#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "bootinfo.h"
#include "pmm64.h"
#include "vmm64.h"
#include "kheap64.h"

static void check(int yes,const char *reason){
    if(!yes){fprintf(stderr,"FAIL kheap64: %s\n",reason);exit(1);}
}
int main(void){
    const size_t bytes=8u*1024u*1024u;
    uint8_t *memory=mmap(NULL,bytes,PROT_READ|PROT_WRITE,
                         MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    check(memory!=MAP_FAILED,"fixture low memory");
    intellect_memory_range_t *ranges=(intellect_memory_range_t *)memory;
    ranges[0]=(intellect_memory_range_t){(uint64_t)(uintptr_t)memory,bytes,
                                         INTELLECT_MEMORY_USABLE,0};
    intellect_bootinfo_t boot={0};
    boot.magic=INTELLECT_BOOTINFO_MAGIC;
    boot.version=INTELLECT_BOOTINFO_VERSION;
    boot.header_size=sizeof(boot);
    boot.boot_method=INTELLECT_BOOT_UEFI;
    boot.flags=INTELLECT_FLAG_MEMORY_MAP_VALID;
    boot.memory_map_addr=(uint64_t)(uintptr_t)ranges;
    boot.memory_map_count=1;
    boot.memory_map_stride=sizeof(*ranges);
    boot.framebuffer_addr=0xE0000000;
    boot.framebuffer_size=4096;
    intellect_pmm_exclusion_t excl[]={ {(uint64_t)(uintptr_t)memory,4096} };
    check(intellect_kheap64_alloc(10,16)==NULL,"not initialized");
    check(intellect_pmm_init(&boot,excl,1),"pmm init");
    uint64_t *root=(uint64_t *)(void *)memory;
    root[0]=UINT64_C(0x200003);
    check(intellect_vmm64_init((uint64_t)(uintptr_t)root),"vmm init");
    check(intellect_kheap64_init(),"heap init");
    check(intellect_kheap64_ready(),"heap ready");
    check(!intellect_kheap64_init(),"no repeat init");
    check(!intellect_kheap64_alloc(0,16),"size zero");
    check(!intellect_kheap64_alloc(1,0),"alignment zero");
    check(!intellect_kheap64_alloc(1,24),"alignment not power two");
    check(!intellect_kheap64_alloc(1,8192),"alignment out of range");
    check(!intellect_kheap64_alloc(64u*4096u+1u,16),"max allocation bound");
    check(!intellect_kheap64_free(NULL),"free null");
    check(!intellect_kheap64_free(memory),"foreign pointer");
    check(intellect_kheap64_self_test(),"kernel smoke test");
    const size_t sizes[]={1,31,32,33,63,64,65,128,129,256,257,
                          512,513,1024,1025,2048,2049,4096,4097};
    void *saved[sizeof(sizes)/sizeof(sizes[0])];
    for(size_t j=0;j<sizeof(sizes)/sizeof(sizes[0]);++j){
        saved[j]=intellect_kheap64_alloc(sizes[j],16);
        check(saved[j]!=NULL,"size class allocate");
        check(!((uintptr_t)saved[j]&15u),"16-byte alignment");
        uint8_t *p=saved[j];
        for(size_t k=0;k<sizes[j];++k)check(p[k]==0,"zero initialize");
        for(size_t k=0;k<sizes[j];++k)p[k]=(uint8_t)(j+1);
        check(!intellect_kheap64_free(p+1),"reject interior pointer");
    }
    for(size_t j=0;j<sizeof(sizes)/sizeof(sizes[0]);++j){
        check(intellect_kheap64_free(saved[j]),"size class free");
        check(!intellect_kheap64_free(saved[j]),"double free blocked");
    }
    void *small[1024];
    for(unsigned i=0;i<1024;++i){
        small[i]=intellect_kheap64_alloc(8,16);
        check(small[i]!=NULL,"1024 small objects");
        ((uint8_t *)small[i])[0]=(uint8_t)i;
    }
    for(unsigned i=0;i<1024;++i)
        check(intellect_kheap64_free(small[i]),"reclaim many small objects");
    void *large[4];
    for(unsigned i=0;i<4u;++i){
        large[i]=intellect_kheap64_alloc(64u*4096u,4096);
        check(large[i]!=NULL,"large page run");
        uint8_t *p=large[i];
        p[0]=(uint8_t)(i+1);p[64u*4096u-1u]=(uint8_t)(i+10);
    }
    check(!intellect_kheap64_alloc(32,16),"heap slot exhaustion");
    check(!intellect_kheap64_free((uint8_t *)large[0]+4096),
          "large allocation interior pointer");
    for(unsigned i=0;i<4u;++i){
        uint8_t *p=large[i];
        check(p[0]==i+1&&p[64u*4096u-1u]==i+10,"large payload integrity");
        check(intellect_kheap64_free(large[i]),"large run free");
    }
    intellect_kheap64_stats_t st=intellect_kheap64_stats();
    check(st.live_allocations==0&&st.mapped_pages==0&&st.live_capacity_bytes==0,
          "all frames reclaimed");
    check(st.peak_mapped_pages==INTELLECT_KHEAP64_PAGE_COUNT,
          "observed high-water pages");
    check(intellect_kheap64_self_test(),"repeat smoke test");
    /* Deterministic fragmented-allocation stress (no time or randomness API). */
    struct record { uint8_t *pointer; size_t size; uint8_t tag; } records[128]={0};
    uint32_t seed=0x9E3779B9u;
    for(unsigned t=0;t<10000u;++t){
        seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
        unsigned j=seed%128u;
        if(records[j].pointer){
            uint8_t *p=records[j].pointer;
            check(p[0]==records[j].tag &&
                  p[records[j].size-1u]==records[j].tag,
                  "random live-allocation integrity");
            check(intellect_kheap64_free(p),"random exact-pointer free");
            records[j].pointer=NULL;
        }else{
            size_t size=1u+((seed>>8)%9000u);
            size_t align=(size_t)1u<<((seed>>24)%9u);
            uint8_t *p=intellect_kheap64_alloc(size,align);
            if(p){
                check(((uintptr_t)p&(align-1u))==0u,"random alignment");
                uint8_t tag=(uint8_t)((seed>>16)|1u);
                check(p[0]==0&&p[size-1u]==0,"random zero init");
                p[0]=tag;p[size-1u]=tag;
                records[j]=(struct record){p,size,tag};
            }
        }
    }
    for(unsigned j=0;j<128u;++j)if(records[j].pointer){
        uint8_t *p=records[j].pointer;
        check(p[0]==records[j].tag &&
              p[records[j].size-1u]==records[j].tag,
              "stress survivor integrity");
        check(intellect_kheap64_free(p),"stress survivor free");
    }
    st=intellect_kheap64_stats();
    check(!st.mapped_pages&&!st.live_allocations&&!st.live_capacity_bytes,
          "stress frames reclaimed");
    /* Simulate another kernel component violating the VMM lease invariant:
     * allocator MUST detect the inconsistent PTE instead of freeing its frame.
     */
    void *tamper=intellect_kheap64_alloc(40,16);
    check(tamper!=NULL,"tamper fixture");
    uint16_t first_slot=INTELLECT_KHEAP64_START_SLOT;
    check(intellect_vmm64_unmap(first_slot),"external unmap fixture");
    check(!intellect_kheap64_free(tamper),"external mutation rejected");
    check(!intellect_kheap64_ready(),"allocator fails closed on corruption");
    puts("PASS: KHEAP64 slabs, alignment, zeroing, page runs, OOM, ownership, rollback, invalid/double-free, reclaim");
    return 0;
}
