/* Pure 4-KiB leaf policy for the single-CPU IntellectOS M3-A transition. */
#include <stdint.h>
#include "m3policy64.h"

static int aligned(uint64_t x) { return (x & (INTELLECT_M3_PAGE - 1u)) == 0; }
int intellect_m3_layout_valid(const intellect_m3_layout_t *l) {
    if (!l || !aligned(l->kernel_begin) || !aligned(l->text_end) ||
        !aligned(l->ro_end) || !aligned(l->image_end) ||
        !aligned(l->kernel_reserved_end) || !aligned(l->stack_guard)) return 0;
    if (l->kernel_begin < INTELLECT_M3_PAGE ||
        l->kernel_begin >= l->text_end || l->text_end > l->ro_end ||
        l->ro_end > l->image_end || l->image_end > l->kernel_reserved_end ||
        l->kernel_reserved_end > INTELLECT_M3_LIMIT ||
        l->kernel_reserved_end - l->kernel_begin != UINT64_C(128)*4096u ||
        l->stack_guard < INTELLECT_M3_PAGE ||
        l->stack_guard > INTELLECT_M3_LIMIT - INTELLECT_M3_PAGE ||
        (l->stack_guard >= l->kernel_begin &&
         l->stack_guard < l->kernel_reserved_end)) return 0;
    return 1;
}
uint64_t intellect_m3_leaf_for(const intellect_m3_layout_t *l,
                               uint64_t pa, uint64_t cache_flags) {
    if (!intellect_m3_layout_valid(l) ||
        (pa & (INTELLECT_M3_PAGE - 1u)) || pa >= INTELLECT_M3_LIMIT ||
        (cache_flags & ~(INTELLECT_M3_PWT | INTELLECT_M3_PCD))) return 0;
    if (pa == l->stack_guard) return 0; /* guard must be completely unmapped */
    uint64_t flags = INTELLECT_M3_PRESENT | cache_flags;
    if (pa >= l->kernel_begin && pa < l->text_end) {
        /* Supervisor RX. CR0.WP makes read-only effective in ring0. */
    } else if (pa >= l->text_end && pa < l->ro_end) {
        flags |= INTELLECT_M3_NX; /* supervisor RO + NX */
    } else {
        flags |= INTELLECT_M3_WRITE | INTELLECT_M3_NX;
    }
    return (pa & INTELLECT_M3_PA_MASK) | flags;
}
