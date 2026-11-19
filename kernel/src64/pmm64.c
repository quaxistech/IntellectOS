/* Conservative low-4-GiB physical page manager for Intellect OS.
 * No libc, heap, page-table mutation, firmware access or implicit reclaim.
 * Both bitmaps are part of the loader-reserved native kernel image.
 */
#include <stddef.h>
#include <stdint.h>
#include "pmm64.h"

#define PAGE_COUNT UINT32_C(1048576)
#define BITMAP_BYTES (PAGE_COUNT / 8u)
#define LOW_PROTECTED_PAGES 256u   /* 0..1MiB always reserved */
#define MAP_MAX_RANGES 5300u
#define EXCLUSIONS_MAX 32u

/* managed: eligible conventional pages minus all reservations.
 * busy: pages currently checked out to a caller; never allows freeing holes.
 */
static uint8_t managed[BITMAP_BYTES];
static uint8_t busy[BITMAP_BYTES];
static intellect_pmm_stats_t statistics;
static uint32_t cursor;
static int ready;

static void bytes_zero(uint8_t *p, size_t count) {
    for (size_t i = 0; i < count; ++i) p[i] = 0;
}
static int bit_test(const uint8_t *p, uint32_t i) {
    return (p[i >> 3] & (uint8_t)(1u << (i & 7u))) != 0;
}
static void bit_set(uint8_t *p, uint32_t i) {
    p[i >> 3] |= (uint8_t)(1u << (i & 7u));
}
static void bit_clear(uint8_t *p, uint32_t i) {
    p[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}
/* Reserve any page touched, including partial start/end pages. */
static void exclude(uint64_t base, uint64_t length) {
    if (length == 0 || base >= INTELLECT_PMM_LIMIT) return;
    uint64_t end = base + length;
    if (end > INTELLECT_PMM_LIMIT) end = INTELLECT_PMM_LIMIT;
    uint64_t start_page = base / INTELLECT_PMM_PAGE_SIZE;
    uint64_t end_page = end / INTELLECT_PMM_PAGE_SIZE;
    if ((end & (INTELLECT_PMM_PAGE_SIZE - 1u)) != 0u) ++end_page;
    for (uint64_t p = start_page; p < end_page; ++p)
        bit_clear(managed, (uint32_t)p);
}
/* Add only whole pages; a partially described page is not free. */
static void include_whole(uint64_t base, uint64_t length) {
    if (base >= INTELLECT_PMM_LIMIT) return;
    uint64_t end = base + length;
    if (end > INTELLECT_PMM_LIMIT) end = INTELLECT_PMM_LIMIT;
    uint64_t first = base / INTELLECT_PMM_PAGE_SIZE;
    if ((base & (INTELLECT_PMM_PAGE_SIZE - 1u)) != 0u) ++first;
    uint64_t last = end / INTELLECT_PMM_PAGE_SIZE;
    for (uint64_t p = first; p < last; ++p)
        bit_set(managed, (uint32_t)p);
}
static int valid_region(uint64_t base, uint64_t length) {
    return length != 0 && base <= UINT64_MAX - length;
}
static void reset(void) {
    ready = 0;
    bytes_zero(managed, sizeof(managed));
    bytes_zero(busy, sizeof(busy));
    statistics = (intellect_pmm_stats_t){0, 0, 0};
    cursor = LOW_PROTECTED_PAGES;
}
int intellect_pmm_init(const intellect_bootinfo_t *boot,
                      const intellect_pmm_exclusion_t *exclusions,
                      size_t exclusion_count) {
    reset();
    if (!boot || (exclusion_count && !exclusions) ||
        exclusion_count > EXCLUSIONS_MAX ||
        boot->magic != INTELLECT_BOOTINFO_MAGIC ||
        boot->version != INTELLECT_BOOTINFO_VERSION ||
        boot->header_size != sizeof(*boot) ||
        boot->boot_method != INTELLECT_BOOT_UEFI ||
        (boot->flags & INTELLECT_FLAG_MEMORY_MAP_VALID) == 0 ||
        (boot->flags & INTELLECT_FLAG_MEMORY_MAP_TRUNC) != 0 ||
        boot->memory_map_stride != sizeof(intellect_memory_range_t) ||
        boot->memory_map_count == 0 || boot->memory_map_count > MAP_MAX_RANGES ||
        boot->memory_map_addr == 0 ||
        boot->memory_map_addr > INTELLECT_PMM_LIMIT -
                                (uint64_t)boot->memory_map_count * sizeof(intellect_memory_range_t) ||
        !valid_region(boot->framebuffer_addr, boot->framebuffer_size)) return 0;
    const intellect_memory_range_t *ranges =
        (const intellect_memory_range_t *)(uintptr_t)boot->memory_map_addr;
    /* Validate all descriptors BEFORE making any conventional RAM available. */
    for (uint32_t i = 0; i < boot->memory_map_count; ++i) {
        if (!valid_region(ranges[i].base, ranges[i].length) ||
            ranges[i].type < INTELLECT_MEMORY_USABLE ||
            ranges[i].type > INTELLECT_MEMORY_BAD) return 0;
    }
    for (size_t i = 0; i < exclusion_count; ++i) {
        if (!valid_region(exclusions[i].base, exclusions[i].length)) return 0;
    }
    /* Order-independent precedence: everything except conventional is denied. */
    for (uint32_t i = 0; i < boot->memory_map_count; ++i) {
        if (ranges[i].type == INTELLECT_MEMORY_USABLE)
            include_whole(ranges[i].base, ranges[i].length);
    }
    for (uint32_t i = 0; i < boot->memory_map_count; ++i) {
        if (ranges[i].type != INTELLECT_MEMORY_USABLE)
            exclude(ranges[i].base, ranges[i].length);
    }
    exclude(0, UINT64_C(0x100000));
    exclude(boot->framebuffer_addr, boot->framebuffer_size);
    /* Reserved even when bogus firmware describes table pages as conventional. */
    exclude((uintptr_t)boot, sizeof(*boot));
    exclude(boot->memory_map_addr,
            (uint64_t)boot->memory_map_count * sizeof(intellect_memory_range_t));
    if (boot->acpi_rsdp_addr) exclude(boot->acpi_rsdp_addr, 1);
    if (boot->smbios_addr) exclude(boot->smbios_addr, 1);
    for (size_t i = 0; i < exclusion_count; ++i)
        exclude(exclusions[i].base, exclusions[i].length);
    uint32_t total = 0;
    for (uint32_t i = LOW_PROTECTED_PAGES; i < PAGE_COUNT; ++i)
        if (bit_test(managed, i)) ++total;
    if (total == 0) { reset(); return 0; }
    statistics.managed_pages = total;
    statistics.free_pages = total;
    ready = 1;
    return 1;
}
uint64_t intellect_pmm_alloc_page(void) {
    if (!ready || statistics.free_pages == 0) return 0;
    for (uint32_t searched = 0, i = cursor; searched < PAGE_COUNT; ++searched) {
        if (i == PAGE_COUNT) i = LOW_PROTECTED_PAGES;
        if (bit_test(managed, i) && !bit_test(busy, i)) {
            bit_set(busy, i);
            --statistics.free_pages;
            ++statistics.allocated_pages;
            cursor = i + 1u;
            if (cursor >= PAGE_COUNT) cursor = LOW_PROTECTED_PAGES;
            return (uint64_t)i * INTELLECT_PMM_PAGE_SIZE;
        }
        ++i;
    }
    return 0;
}
int intellect_pmm_free_page(uint64_t physical_address) {
    if (!ready || physical_address >= INTELLECT_PMM_LIMIT ||
        (physical_address & (INTELLECT_PMM_PAGE_SIZE - 1u)) != 0) return 0;
    uint32_t i = (uint32_t)(physical_address / INTELLECT_PMM_PAGE_SIZE);
    if (!bit_test(managed, i) || !bit_test(busy, i)) return 0;
    bit_clear(busy, i);
    ++statistics.free_pages;
    --statistics.allocated_pages;
    if (i < cursor) cursor = i;
    return 1;
}
int intellect_pmm_is_managed(uint64_t physical_address) {
    if (!ready || physical_address >= INTELLECT_PMM_LIMIT ||
        (physical_address & (INTELLECT_PMM_PAGE_SIZE - 1u)) != 0) return 0;
    return bit_test(managed, (uint32_t)(physical_address / INTELLECT_PMM_PAGE_SIZE));
}
int intellect_pmm_is_allocated(uint64_t physical_address) {
    if (!intellect_pmm_is_managed(physical_address)) return 0;
    return bit_test(busy, (uint32_t)(physical_address / INTELLECT_PMM_PAGE_SIZE));
}
intellect_pmm_stats_t intellect_pmm_stats(void) { return statistics; }
