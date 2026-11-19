#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "pmm64.h"
#include "vmm64.h"

static void expect(int yes, const char *why) {
    if (!yes) { fprintf(stderr, "FAIL: %s\n", why); exit(1); }
}
int main(void) {
    const size_t size=4096u*1024u;
    uint8_t *block=mmap(NULL,size,PROT_READ|PROT_WRITE,
              MAP_ANONYMOUS|MAP_PRIVATE|MAP_32BIT,-1,0);
    expect(block!=MAP_FAILED,"low physical fixture");
    expect(((uintptr_t)block & 4095u)==0u,"page aligned");
    intellect_memory_range_t *map=(intellect_memory_range_t *)block;
    map[0]=(intellect_memory_range_t){(uint64_t)(uintptr_t)block, size,
                                    INTELLECT_MEMORY_USABLE, 0u};
    intellect_bootinfo_t boot={0};
    boot.magic=INTELLECT_BOOTINFO_MAGIC;
    boot.version=INTELLECT_BOOTINFO_VERSION;
    boot.header_size=sizeof(boot);
    boot.boot_method=INTELLECT_BOOT_UEFI;
    boot.flags=INTELLECT_FLAG_MEMORY_MAP_VALID;
    boot.memory_map_addr=(uint64_t)(uintptr_t)map;
    boot.memory_map_count=1u;
    boot.memory_map_stride=sizeof(*map);
    boot.framebuffer_addr=UINT64_C(0xE0000000);
    boot.framebuffer_size=4096u;
    const intellect_pmm_exclusion_t extra[]={
        {(uint64_t)(uintptr_t)block, 4096u} /* page-table root and map */
    };
    expect(intellect_pmm_init(&boot,extra,1u),"PMM initialization");
    uint64_t root=(uint64_t)(uintptr_t)block;
    uint64_t *pml4=(uint64_t *)(uintptr_t)root;
    pml4[0]=UINT64_C(0x200003);
    expect(intellect_vmm64_init(root),"VMM initialization");
    expect(intellect_vmm64_ready(),"ready");
    expect((pml4[256] & 7u)==3u,"PML4 supervisor present RW");
    uint64_t *pdpt=(uint64_t *)(uintptr_t)(pml4[256] & UINT64_C(0x000FFFFFFFFFF000));
    uint64_t *pd=(uint64_t *)(uintptr_t)(pdpt[0] & UINT64_C(0x000FFFFFFFFFF000));
    uint64_t *pt=(uint64_t *)(uintptr_t)(pd[0] & UINT64_C(0x000FFFFFFFFFF000));
    expect(pt[0]==0u,"unmapped slot 0 guard");
    uint64_t page=intellect_pmm_alloc_page();
    expect(page!=0,"frame allocated");
    expect(intellect_pmm_is_allocated(page),"busy check");
    expect(!intellect_vmm64_map(0,page,1),"guard cannot map");
    expect(!intellect_vmm64_map(512,page,1),"range cannot map");
    expect(!intellect_vmm64_map(2,page+1,1),"unaligned rejected");
    expect(intellect_vmm64_map(2,page,1),"map RW frame");
    expect(!intellect_vmm64_map(2,page,1),"duplicate mapping rejected");
    expect((pt[2] & UINT64_C(0x8000000000000007))==
           UINT64_C(0x8000000000000003),"NX+supervisor+RW");
    uint64_t queried=0;int writable=0;
    expect(intellect_vmm64_query(2,&queried,&writable),"query");
    expect(queried==page && writable,"query value");
    expect(intellect_vmm64_unmap(2),"unmap");
    expect(!intellect_vmm64_unmap(2),"double unmap denied");
    expect(intellect_pmm_free_page(page),"free unmapped frame");
    expect(!intellect_vmm64_map(2,page,1),"free frame cannot map");
    expect(intellect_vmm64_self_test(),"VMM smoke test");
    puts("PASS: VMM64 supervisor NX high-half slots, guard, map/query/unmap, self-test");
    return 0;
}
