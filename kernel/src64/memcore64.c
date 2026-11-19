/* Boot-time exception install and explicit reservations for the physical
 * allocator. Critical allocations remain EfiLoaderCode/EfiLoaderData in the
 * normalized map; explicit exclusions guard against map classification bugs.
 */
#include <stddef.h>
#include <stdint.h>
#include "memcore64.h"
#include "pmm64.h"

extern void intellect64_idt_install(void);
extern void intellect64_memory_checkpoint(uint32_t stage);
#define PAGE UINT64_C(4096)

static int inside_bootstrap(uint64_t base, uint64_t size) {
    return base != 0 && (base & (PAGE - 1u)) == 0 &&
           size != 0 && base <= INTELLECT_PMM_LIMIT - size;
}
static int intersects(intellect_pmm_exclusion_t a,
                      intellect_pmm_exclusion_t b) {
    return a.base < b.base + b.length && b.base < a.base + a.length;
}
int intellect64_memory_core_start(const intellect_bootinfo_t *boot,
                                 uint64_t bootstrap_cr3,
                                 uint64_t stack_top,
                                 uint64_t kernel_base) {
    intellect64_memory_checkpoint(1u); /* before GDT/IDT/TSS */
    intellect64_idt_install();
    intellect64_memory_checkpoint(2u); /* after GDT/IDT/TSS */
    const uint64_t stack_bytes = (uint64_t)INTELLECT_STACK64_PAGES * PAGE;
    if (!boot || stack_top < stack_bytes ||
        !inside_bootstrap(kernel_base,(uint64_t)INTELLECT_KERNEL64_PAGES*PAGE) ||
        !inside_bootstrap(bootstrap_cr3,(uint64_t)INTELLECT_TABLE64_PAGES*PAGE) ||
        !inside_bootstrap(stack_top-stack_bytes,stack_bytes) ||
        !inside_bootstrap((uint64_t)(uintptr_t)boot,
                          (uint64_t)INTELLECT_BOOTINFO64_PAGES*PAGE)) return 0;
    const intellect_pmm_exclusion_t reserved[] = {
        { kernel_base, (uint64_t)INTELLECT_KERNEL64_PAGES * PAGE },
        { stack_top-stack_bytes, stack_bytes },
        { bootstrap_cr3, (uint64_t)INTELLECT_TABLE64_PAGES * PAGE },
        { (uint64_t)(uintptr_t)boot, (uint64_t)INTELLECT_BOOTINFO64_PAGES * PAGE }
    };
    for (size_t i=0; i<4; ++i)
        for (size_t j=i+1; j<4; ++j)
            if (intersects(reserved[i],reserved[j])) return 0;
    int ok=intellect_pmm_init(boot,reserved,4u);
    intellect64_memory_checkpoint(ok ? 3u : 9u);
    return ok;
}
