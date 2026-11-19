#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include "pmm64.h"

static void expect(int expression, const char *message) {
    if (!expression) { fprintf(stderr,"FAIL: %s\n",message); exit(1); }
}
int main(void) {
    /* BootInfo map pointer intentionally low, to meet Stage E ABI. */
    void *buffer = mmap(NULL,4096,PROT_READ|PROT_WRITE,
                        MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    expect(buffer != MAP_FAILED,"mmap host fixture");
    intellect_memory_range_t *m = buffer;
    m[0] = (intellect_memory_range_t){0x100000u, 20u * 4096u, INTELLECT_MEMORY_USABLE,0};
    m[1] = (intellect_memory_range_t){0x103000u, 2u * 4096u, INTELLECT_MEMORY_RESERVED,0};
    m[2] = (intellect_memory_range_t){0x10A000u, 4096u, INTELLECT_MEMORY_ACPI_NVS,0};
    m[3] = (intellect_memory_range_t){0x100000000u,4096u,INTELLECT_MEMORY_USABLE,0};
    intellect_bootinfo_t boot = {0};
    boot.magic=INTELLECT_BOOTINFO_MAGIC;
    boot.version=INTELLECT_BOOTINFO_VERSION;
    boot.header_size=sizeof(boot);
    boot.boot_method=INTELLECT_BOOT_UEFI;
    boot.flags=INTELLECT_FLAG_MEMORY_MAP_VALID;
    boot.memory_map_addr=(uint64_t)(uintptr_t)m;
    boot.memory_map_count=4;
    boot.memory_map_stride=sizeof(*m);
    boot.framebuffer_addr=0x200000u;
    boot.framebuffer_size=4096;
    const intellect_pmm_exclusion_t extra[]={{0x107000u,2u*4096u}};
    expect(intellect_pmm_init(&boot,extra,1),"initialize");
    intellect_pmm_stats_t s=intellect_pmm_stats();
    expect(s.managed_pages==15u,"reserved and explicit exclusion precedence");
    expect(!intellect_pmm_is_managed(0x103000u),"reserved map wins");
    expect(!intellect_pmm_is_managed(0x107000u),"manual exclusion wins");
    expect(!intellect_pmm_is_managed(0x10A000u),"ACPI NVS must not be freed");
    uint64_t a[15];
    for (unsigned i=0;i<15u;++i) {
        a[i]=intellect_pmm_alloc_page();
        expect(a[i]>=0x100000u,"alloc range");
        expect(intellect_pmm_is_managed(a[i]),"allocated page was managed");
        for(unsigned j=0;j<i;++j) expect(a[i]!=a[j],"no duplicate pages");
    }
    expect(intellect_pmm_alloc_page()==0,"OOM");
    expect(!intellect_pmm_free_page(0x103000u),"cannot free non-managed");
    expect(!intellect_pmm_free_page(a[0]+1u),"alignment");
    expect(intellect_pmm_free_page(a[0]),"free first time");
    expect(!intellect_pmm_free_page(a[0]),"double free rejected");
    expect(intellect_pmm_alloc_page()==a[0],"freed page reused");
    expect(intellect_pmm_stats().allocated_pages==15u,"accounting");
    expect(!intellect_pmm_free_page(0),"null physical page denied");
    /* Reordering the firmware descriptors must not change precedence. */
    intellect_memory_range_t temporary=m[0];m[0]=m[1];m[1]=temporary;
    expect(intellect_pmm_init(&boot,extra,1),"unsorted map accepted");
    expect(intellect_pmm_stats().managed_pages==15u,"order-independent reserved wins");
    /* The framebuffer must be reserved even if a broken map calls it usable. */
    boot.framebuffer_addr=0x100000u;
    expect(intellect_pmm_init(&boot,extra,1),"framebuffer overlap accepted conservatively");
    expect(intellect_pmm_stats().managed_pages==14u,"framebuffer page excluded");
    boot.framebuffer_addr=0x200000u;
    temporary=m[0];m[0]=m[1];m[1]=temporary;
    m[1].length = UINT64_MAX;
    expect(!intellect_pmm_init(&boot,extra,1),"overflow rejected");
    expect(intellect_pmm_alloc_page()==0,"fail-closed after invalid input");
    m[1].length=8192u;
    boot.memory_map_count=0;
    expect(!intellect_pmm_init(&boot,extra,1),"empty map rejected");
    printf("PASS: PMM64 overlap, reserved pages, exhaustion, double-free, bad map, accounting\n");
    return 0;
}
