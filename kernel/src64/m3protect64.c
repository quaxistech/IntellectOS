/* M3-A: harden the existing loader identity mappings before user mode exists.
 * This is NOT a process address-space manager. All mappings remain U/S=0.
 * Assumes one CPU, IRQs disabled, ordinary 2MiB bootstrap hugepage entries,
 * NXE and CR0.WP already enabled by intellect_vmm64_init().
 * No firmware API, heap allocations or dynamic relocations.
 */
#include <stddef.h>
#include <stdint.h>
#include "m3protect64.h"
#include "m3policy64.h"
#include "memcore64.h"
#include "pmm64.h"

#define HUGE UINT64_C(0x200000)
#define SIZE_1G UINT64_C(0x40000000)
#define PAGES_PER_TABLE 512u
#define MAX_SPLITS 3u /* at most 2 kernel PDEs and 1 guard PDE */
#define CR0_WP (UINT64_C(1) << 16)
#define EFER_MSR UINT32_C(0xC0000080)
#define EFER_NXE (UINT64_C(1) << 11)

extern __attribute__((visibility("hidden"))) void intellect64_entry(void);
extern __attribute__((visibility("hidden"))) uint8_t __intellect_text_end[];
extern __attribute__((visibility("hidden"))) uint8_t __intellect_rodata_end[];
extern __attribute__((visibility("hidden"))) uint8_t __intellect_bss_end[];

static int ready;
static uint64_t identity_cr3;
static intellect_m3_layout_t current_layout;

typedef struct {
    uint16_t pdpt_index;
    uint16_t pd_index;
    uint64_t frame;
    uint64_t previous_pde;
} split_t;

static int valid_table_frame(uint64_t pa) {
    return pa != 0 && pa <= INTELLECT_M3_LIMIT - 4096u &&
           !(pa & (INTELLECT_M3_PAGE - 1u));
}
static uint64_t *table_at(uint64_t pa) { return (uint64_t *)(uintptr_t)pa; }
static uint64_t physical(uint64_t pte) { return pte & INTELLECT_M3_PA_MASK; }
static uint64_t flags_read_cr0(void) {
    uint64_t x;
    __asm__ volatile("mov %%cr0, %0" : "=r"(x));
    return x;
}
static uint64_t flags_read_efer(void) {
    uint32_t lo,hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(EFER_MSR));
    return ((uint64_t)hi << 32) | lo;
}
static int flags_ready(void) {
    return (flags_read_cr0() & CR0_WP) &&
           (flags_read_efer() & EFER_NXE);
}
static void reload_tlb(uint64_t cr3) {
    /* Current bootstrap address space has no global mappings or PCID. */
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}
static int is_split(const split_t *splits, unsigned n, unsigned g, unsigned j) {
    for (unsigned k=0;k<n;++k)
        if (splits[k].pdpt_index==g && splits[k].pd_index==j) return 1;
    return 0;
}
static int append_split(split_t *splits, unsigned *n, uint64_t pa) {
    const unsigned g=(unsigned)(pa / SIZE_1G);
    const unsigned j=(unsigned)((pa % SIZE_1G) / HUGE);
    if (g >= 4u) return 0; /* kernel/stack allocated below 4 GiB */
    if (is_split(splits,*n,g,j)) return 1;
    if (*n == MAX_SPLITS) return 0;
    splits[*n]=(split_t){(uint16_t)g,(uint16_t)j,0,0};
    ++*n;
    return 1;
}
static int validate_loader_map(uint64_t cr3) {
    if (!valid_table_frame(cr3)) return 0;
    uint64_t *pml4=table_at(cr3);
    const uint64_t root=pml4[0];
    if (!(root & INTELLECT_M3_PRESENT) || (root & INTELLECT_M3_USER) ||
        !valid_table_frame(physical(root))) return 0;
    uint64_t *pdpt=table_at(physical(root));
    for (unsigned g=0;g<512u;++g) {
        const uint64_t p=pdpt[g];
        if (!(p & INTELLECT_M3_PRESENT)) {
            if (g<4u) return 0;
            continue;
        }
        if ((p & (INTELLECT_M3_USER | INTELLECT_M3_PS)) ||
            !valid_table_frame(physical(p))) return 0;
        uint64_t *pd=table_at(physical(p));
        for (unsigned j=0;j<512u;++j) {
            const uint64_t e=pd[j];
            if (!(e & INTELLECT_M3_PRESENT)) {
                if (g<4u) return 0;
                continue;
            }
            if (!(e & INTELLECT_M3_PS) || (e & INTELLECT_M3_USER) ||
                (e & UINT64_C(0x1000)) || /* hugepage PAT not supported */
                (e & INTELLECT_M3_2M_PA_MASK) !=
                ((uint64_t)g*SIZE_1G + (uint64_t)j*HUGE)) return 0;
        }
    }
    return 1;
}
int intellect_m3_protect64_ready(void) { return ready; }
int intellect_m3_protect64_init(uint64_t cr3,
                               uint64_t kernel_base, uint64_t stack_top) {
    if (ready || !flags_ready() || !valid_table_frame(cr3) ||
        !valid_table_frame(kernel_base) ||
        stack_top < (uint64_t)INTELLECT_STACK64_PAGES*4096u ||
        stack_top > INTELLECT_M3_LIMIT ||
        (stack_top & (INTELLECT_M3_PAGE-1u))) return 0;
    const uint64_t guard=stack_top-(uint64_t)INTELLECT_STACK64_PAGES*4096u;
    intellect_m3_layout_t l={
        kernel_base, (uint64_t)(uintptr_t)__intellect_text_end,
        (uint64_t)(uintptr_t)__intellect_rodata_end,
        (uint64_t)(uintptr_t)__intellect_bss_end,
        kernel_base+(uint64_t)INTELLECT_KERNEL64_PAGES*4096u, guard
    };
    if ((uint64_t)(uintptr_t)&intellect64_entry != kernel_base ||
        !intellect_m3_layout_valid(&l) || !validate_loader_map(cr3)) return 0;
    split_t splits[MAX_SPLITS]; unsigned count=0;
    if (!append_split(splits,&count,kernel_base) ||
        !append_split(splits,&count,l.kernel_reserved_end-1u) ||
        !append_split(splits,&count,guard)) return 0;
    uint64_t *pml4=table_at(cr3);
    uint64_t *pdpt=table_at(physical(pml4[0]));
    /* PREPARE: allocate and fully populate the required 4-KiB leaf tables.
     * Nothing is published, and failure returns the frames to PMM. */
    for (unsigned i=0;i<count;++i) {
        uint64_t *pd=table_at(physical(pdpt[splits[i].pdpt_index]));
        uint64_t old=pd[splits[i].pd_index];
        splits[i].previous_pde=old;
        if (!(old & INTELLECT_M3_PRESENT) || !(old & INTELLECT_M3_PS))
            goto fail;
        uint64_t frame=intellect_pmm_alloc_page();
        if (!frame) goto fail;
        splits[i].frame=frame;
        uint64_t *pt=table_at(frame);
        uint64_t base=(uint64_t)splits[i].pdpt_index*SIZE_1G +
                      (uint64_t)splits[i].pd_index*HUGE;
        const uint64_t caching=old & (INTELLECT_M3_PCD|INTELLECT_M3_PWT);
        for (unsigned j=0;j<PAGES_PER_TABLE;++j) {
            uint64_t address=base+(uint64_t)j*INTELLECT_M3_PAGE;
            uint64_t leaf=intellect_m3_leaf_for(&l,address,caching);
            if (address!=guard && leaf==0) goto fail;
            pt[j]=leaf;
        }
    }
    /* PUBLISH: no fallible operation follows. Every 2MiB identity PDE not
     * containing kernel code is NX. The split leaf tables select RX/RO/RW.
     * No concurrent CPU or IRQ can observe partially updated translations. */
    for (unsigned g=0;g<512u;++g) {
        if (!(pdpt[g] & INTELLECT_M3_PRESENT)) continue;
        uint64_t *pd=table_at(physical(pdpt[g]));
        for (unsigned j=0;j<512u;++j) {
            if (!(pd[j] & INTELLECT_M3_PRESENT) ||
                is_split(splits,count,g,j)) continue;
            pd[j]|=INTELLECT_M3_NX;
        }
    }
    for (unsigned i=0;i<count;++i) {
        uint64_t *pd=table_at(physical(pdpt[splits[i].pdpt_index]));
        pd[splits[i].pd_index] = splits[i].frame |
            INTELLECT_M3_PRESENT | INTELLECT_M3_WRITE;
    }
    reload_tlb(cr3);
    identity_cr3=cr3;
    current_layout=l;
    ready=1;
    return 1;
fail:
    for (unsigned i=0;i<count;++i) {
        if (splits[i].frame && !intellect_pmm_free_page(splits[i].frame))
            return 0; /* permanent fail-closed */
    }
    return 0;
}
/* Query leaf effective protection for a physical identity address. */
static int query(uint64_t pa, uint64_t *leaf) {
    if (!ready || pa>=INTELLECT_M3_LIMIT) return 0;
    uint64_t *pml4=table_at(identity_cr3);
    uint64_t *pdpt=table_at(physical(pml4[0]));
    unsigned g=(unsigned)(pa / SIZE_1G);
    uint64_t e=pdpt[g];
    if (!(e & INTELLECT_M3_PRESENT)) return 0;
    uint64_t *pd=table_at(physical(e));
    e=pd[(pa%SIZE_1G)/HUGE];
    if (!(e & INTELLECT_M3_PRESENT)) return 0;
    if (e & INTELLECT_M3_PS) {
        *leaf=e;
    } else {
        uint64_t *pt=table_at(physical(e));
        e=pt[(pa%HUGE)/INTELLECT_M3_PAGE];
        if (!(e & INTELLECT_M3_PRESENT)) return 0;
        *leaf=e;
    }
    return 1;
}
static int exact(uint64_t pa, int writable, int executable) {
    uint64_t e=0;
    if (!query(pa,&e)) return 0;
    return !(e & INTELLECT_M3_USER) &&
        !!(e & INTELLECT_M3_WRITE)==writable &&
        !(!!(e & INTELLECT_M3_NX))==executable;
}
int intellect_m3_protect64_self_test(void) {
    if (!ready || !flags_ready()) return 0;
    const intellect_m3_layout_t *l=&current_layout;
    if (!exact(l->kernel_begin,0,1) ||
        !exact(l->text_end-INTELLECT_M3_PAGE,0,1) ||
        (l->text_end<l->ro_end && !exact(l->text_end,0,0)) ||
        !exact(l->image_end-INTELLECT_M3_PAGE,1,0) ||
        !exact(UINT64_C(0),1,0) ||
        !exact(l->kernel_reserved_end-INTELLECT_M3_PAGE,1,0)) return 0;
    uint64_t e=0;
    if (query(l->stack_guard,&e) ||
        !exact(l->stack_guard+INTELLECT_M3_PAGE,1,0)) return 0;
    /* Check all present bootstrap leaves, not just selected samples. */
    uint64_t *pml4=table_at(identity_cr3);
    uint64_t *pdpt=table_at(physical(pml4[0]));
    for (unsigned g=0;g<512u;++g) {
        if (!(pdpt[g] & INTELLECT_M3_PRESENT)) continue;
        uint64_t *pd=table_at(physical(pdpt[g]));
        for (unsigned j=0;j<512u;++j) {
            uint64_t p=pd[j];
            if (!(p & INTELLECT_M3_PRESENT)) continue;
            if (p & INTELLECT_M3_USER) return 0;
            if (p & INTELLECT_M3_PS) {
                if (!(p & INTELLECT_M3_NX)) return 0;
            } else {
                uint64_t *pt=table_at(physical(p));
                for (unsigned k=0;k<512u;++k) {
                    uint64_t e2=pt[k];
                    if (!(e2 & INTELLECT_M3_PRESENT)) continue;
                    uint64_t pa=(uint64_t)g*SIZE_1G +
                        (uint64_t)j*HUGE+(uint64_t)k*INTELLECT_M3_PAGE;
                    if (e2 & INTELLECT_M3_USER) return 0;
                    if (pa>=l->kernel_begin && pa<l->text_end) {
                        if ((e2 & INTELLECT_M3_WRITE) ||
                            (e2 & INTELLECT_M3_NX)) return 0;
                    } else if (!(e2 & INTELLECT_M3_NX)) return 0;
                }
            }
        }
    }
    return 1;
}
