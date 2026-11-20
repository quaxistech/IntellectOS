#ifndef INTELLECT_M3PROTECT64_H
#define INTELLECT_M3PROTECT64_H
#include <stdint.h>
/* Called exactly once after M1 VMM and M2 heap self-tests, with IRQs disabled.
 * Existing 0..4GiB identity map is retained for physical-memory access,
 * but non-kernel code pages are NX, kernel .text RX, .rodata RO/NX.
 * Leaves GDT/IDT/TSS and BootInfo v1 unchanged.
 */
int intellect_m3_protect64_init(uint64_t bootstrap_cr3,
                               uint64_t kernel_base, uint64_t stack_top);
int intellect_m3_protect64_self_test(void);
int intellect_m3_protect64_ready(void);
#endif
