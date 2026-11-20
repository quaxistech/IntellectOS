#ifndef INTELLECT_M3POLICY64_H
#define INTELLECT_M3POLICY64_H
#include <stdint.h>

/* One-CPU, 4-KiB leaf policy; all physical addresses in [0,4GiB). */
#define INTELLECT_M3_PAGE UINT64_C(4096)
#define INTELLECT_M3_PRESENT UINT64_C(0x001)
#define INTELLECT_M3_WRITE UINT64_C(0x002)
#define INTELLECT_M3_USER UINT64_C(0x004)
#define INTELLECT_M3_PWT UINT64_C(0x008)
#define INTELLECT_M3_PCD UINT64_C(0x010)
#define INTELLECT_M3_PS UINT64_C(0x080)
#define INTELLECT_M3_NX UINT64_C(0x8000000000000000)
#define INTELLECT_M3_PA_MASK UINT64_C(0x000FFFFFFFFFF000)
#define INTELLECT_M3_2M_PA_MASK UINT64_C(0x000FFFFFFFE00000)
#define INTELLECT_M3_LIMIT UINT64_C(0x100000000)

typedef struct {
    uint64_t kernel_begin; /* equals physical address of the kernel entry */
    uint64_t text_end;     /* page-aligned end of .text */
    uint64_t ro_end;       /* page-aligned end of .rodata */
    uint64_t image_end;    /* page-aligned end of .bss, not raw .bin size */
    uint64_t kernel_reserved_end; /* exclusive, 128 allocated pages */
    uint64_t stack_guard;  /* bottom of loader stack: one no-access page */
} intellect_m3_layout_t;

/* Pure policy: easy to exercise on a normal host without privileged opcodes. */
int intellect_m3_layout_valid(const intellect_m3_layout_t *l);
uint64_t intellect_m3_leaf_for(const intellect_m3_layout_t *l,
                               uint64_t pa, uint64_t cache_flags);
#endif
