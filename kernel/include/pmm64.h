#ifndef INTELLECT_PMM64_H
#define INTELLECT_PMM64_H
#include <stddef.h>
#include <stdint.h>
#include "bootinfo.h"

/* Stage M0: only the first 4 GiB is identity-mapped and allocatable.
 * All physical addresses are 4096-byte aligned; zero means failure.
 * The allocator is single-CPU, non-reentrant, and does not zero returned RAM.
 */
#define INTELLECT_PMM_PAGE_SIZE UINT64_C(4096)
#define INTELLECT_PMM_LIMIT UINT64_C(0x100000000)

typedef struct { uint64_t base, length; } intellect_pmm_exclusion_t;
typedef struct {
    uint32_t managed_pages;
    uint32_t free_pages;
    uint32_t allocated_pages;
} intellect_pmm_stats_t;

/* Fail closed on map errors, conflicting immutable reservations or no memory.
 * The normalized map MUST already have been securely acquired before calling.
 */
int intellect_pmm_init(const intellect_bootinfo_t *boot,
                      const intellect_pmm_exclusion_t *exclusions,
                      size_t exclusion_count);
uint64_t intellect_pmm_alloc_page(void);
int intellect_pmm_free_page(uint64_t physical_address);
intellect_pmm_stats_t intellect_pmm_stats(void);
int intellect_pmm_is_managed(uint64_t physical_address);
int intellect_pmm_is_allocated(uint64_t physical_address);
#endif
