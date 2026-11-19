/* Intellect OS M1: supervisor-only high-half 2-MiB scratch mapping window.
 * Bootstrap's 0..4 GiB identity map is intentionally left intact.
 * All page tables are allocated below 4 GiB and initialized via identity map.
 * Strictly single CPU; do not enable interrupts, SMP or preemption.
 */
#include <stddef.h>
#include <stdint.h>
#include "pmm64.h"
#include "vmm64.h"

#define PTE_PRESENT UINT64_C(0x001)
#define PTE_WRITABLE UINT64_C(0x002)
#define PTE_NX UINT64_C(0x8000000000000000)
#define PTE_ADDR UINT64_C(0x000FFFFFFFFFF000)
#define EFER_NXE UINT64_C(0x800)
#define EFER_MSR UINT32_C(0xC0000080)

static uint64_t *slot_table;
static int initialized;

static int valid_frame(uint64_t pa) {
    return pa >= UINT64_C(0x100000) && pa < INTELLECT_PMM_LIMIT &&
           (pa & UINT64_C(4095)) == 0u;
}
static void zero_page(uint64_t address) {
    uint64_t *p=(uint64_t *)(uintptr_t)address;
    for (unsigned i=0;i<512u;++i) p[i]=0u;
}
static void flush_page(uint16_t slot) {
#ifdef INTELLECT_VMM64_HOST_TEST
    (void)slot;
#else
    const uintptr_t virtual_addr=(uintptr_t)(INTELLECT_VMM64_BASE+
                                     (uint64_t)slot*INTELLECT_PMM_PAGE_SIZE);
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
#endif
}
static int enable_nxe(void) {
#ifdef INTELLECT_VMM64_HOST_TEST
    return 1;
#else
    uint32_t largest, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(largest),"=b"(ebx),"=c"(ecx),"=d"(edx)
                         : "a"(UINT32_C(0x80000000)), "c"(0));
    (void)ebx;(void)ecx;
    if (largest < UINT32_C(0x80000001)) return 0;
    __asm__ volatile("cpuid" : "=a"(largest),"=b"(ebx),"=c"(ecx),"=d"(edx)
                         : "a"(UINT32_C(0x80000001)), "c"(0));
    if ((edx & (UINT32_C(1)<<20)) == 0u) return 0;
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(EFER_MSR));
    if ((lo & (uint32_t)EFER_NXE)==0u) {
        lo |= (uint32_t)EFER_NXE;
        __asm__ volatile("wrmsr" : : "c"(EFER_MSR), "a"(lo), "d"(hi) : "memory");
    }
    return 1;
#endif
}
int intellect_vmm64_ready(void) { return initialized; }
int intellect_vmm64_init(uint64_t bootstrap_cr3) {
    if (initialized || !valid_frame(bootstrap_cr3)) return 0;
    /* This is the Stage E CR3, not a new address space. */
    uint64_t *pml4=(uint64_t *)(uintptr_t)bootstrap_cr3;
    if ((pml4[0] & PTE_PRESENT)==0u || pml4[256]!=0u) return 0;
    if (!enable_nxe()) return 0;
#ifndef INTELLECT_VMM64_HOST_TEST
    /* Supervisor-mode writes must also respect read-only PTEs (CR0.WP). */
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if ((cr0 & (UINT64_C(1)<<16)) == 0u) {
        cr0 |= UINT64_C(1)<<16;
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    }
#endif
    uint64_t allocated[3]={0,0,0};
    for (unsigned i=0;i<3u;++i) {
        allocated[i]=intellect_pmm_alloc_page();
        if (!allocated[i]) {
            for (unsigned j=0;j<i;++j) (void)intellect_pmm_free_page(allocated[j]);
            return 0;
        }
        zero_page(allocated[i]);
    }
    uint64_t *pdpt=(uint64_t *)(uintptr_t)allocated[0];
    uint64_t *pd=(uint64_t *)(uintptr_t)allocated[1];
    uint64_t *pt=(uint64_t *)(uintptr_t)allocated[2];
    pdpt[0]=allocated[1] | PTE_PRESENT | PTE_WRITABLE;
    pd[0]=allocated[2] | PTE_PRESENT | PTE_WRITABLE;
    /* Publish PML4 last: no partially initialized mappings become visible. */
    pml4[256]=allocated[0] | PTE_PRESENT | PTE_WRITABLE;
    slot_table=pt;
    initialized=1;
    /* The window has never been mapped before. flush_page covers slot zero. */
    flush_page(0u);
    return 1;
}
int intellect_vmm64_map(uint16_t slot, uint64_t physical, int writable) {
    if (!initialized || slot<INTELLECT_VMM64_FIRST_SLOT ||
        slot>=INTELLECT_VMM64_SLOTS || !valid_frame(physical) ||
        !intellect_pmm_is_allocated(physical) ||
        (slot_table[slot] & PTE_PRESENT)) return 0;
    /* Force U/S=0, G=0, NX=1; only the W bit is caller selectable. */
    slot_table[slot]=(physical & PTE_ADDR) | PTE_PRESENT | PTE_NX |
                     (writable ? PTE_WRITABLE : 0u);
    flush_page(slot);
    return 1;
}
int intellect_vmm64_unmap(uint16_t slot) {
    if (!initialized || slot<INTELLECT_VMM64_FIRST_SLOT ||
        slot>=INTELLECT_VMM64_SLOTS || !(slot_table[slot] & PTE_PRESENT)) return 0;
    slot_table[slot]=0;
    flush_page(slot);
    return 1;
}
int intellect_vmm64_query(uint16_t slot, uint64_t *physical, int *writable) {
    if (!initialized || slot>=INTELLECT_VMM64_SLOTS ||
        !(slot_table[slot] & PTE_PRESENT)) return 0;
    uint64_t pte=slot_table[slot];
    if (physical) *physical=pte & PTE_ADDR;
    if (writable) *writable=(pte & PTE_WRITABLE)!=0u;
    return 1;
}
int intellect_vmm64_self_test(void) {
    if (!initialized) return 0;
    uint64_t phys=intellect_pmm_alloc_page();
    if (!phys) return 0;
    /* Failure paths do not free a still-mapped frame. */
    if (!intellect_vmm64_map(1u,phys,1)) {
        (void)intellect_pmm_free_page(phys);
        return 0;
    }
    uint64_t query=0;
    int writable=0;
    if (!intellect_vmm64_query(1u,&query,&writable) || query!=phys || !writable) {
        (void)intellect_vmm64_unmap(1u);
        (void)intellect_pmm_free_page(phys);
        return 0;
    }
#ifndef INTELLECT_VMM64_HOST_TEST
    volatile uint64_t *alias=(volatile uint64_t *)(uintptr_t)
        (INTELLECT_VMM64_BASE + INTELLECT_PMM_PAGE_SIZE);
#else
    volatile uint64_t *alias=(volatile uint64_t *)(uintptr_t)phys;
#endif
    volatile uint64_t *identity=(volatile uint64_t *)(uintptr_t)phys;
    *alias=UINT64_C(0x123456789ABCDEF0);
    int success=(*identity==UINT64_C(0x123456789ABCDEF0));
    *alias=0u;
    if (!intellect_vmm64_unmap(1u)) return 0;
    if (intellect_vmm64_query(1u,0,0)) success=0;
    if (!intellect_pmm_free_page(phys)) success=0;
    return success;
}
