#ifndef INTELLECT_VMM64_H
#define INTELLECT_VMM64_H
#include <stdint.h>

/* x86-64 high-half scratch window: PML4[256], PDPT[0], PD[0], PT[0..511].
 * Non-global, supervisor-only, 4-KiB mappings; slot zero is an unmapped guard.
 * This is a bounded first VMM stage, NOT a complete address-space manager.
 */
#define INTELLECT_VMM64_BASE UINT64_C(0xFFFF800000000000)
#define INTELLECT_VMM64_SLOTS 512u
#define INTELLECT_VMM64_FIRST_SLOT 1u

/* Caller must first initialize the single-CPU physical page manager.
 * Returns 0 if NX is unsupported, a table already exists, or allocation fails.
 */
int intellect_vmm64_init(uint64_t bootstrap_cr3);

/* Only PMM-allocated frames may be mapped; no executable mappings are exposed.
 * A mapped frame must not be released until its slot is unmapped.
 */
int intellect_vmm64_map(uint16_t slot, uint64_t physical, int writable);
int intellect_vmm64_unmap(uint16_t slot);
int intellect_vmm64_query(uint16_t slot, uint64_t *physical, int *writable);
int intellect_vmm64_ready(void);
int intellect_vmm64_self_test(void);
#endif
