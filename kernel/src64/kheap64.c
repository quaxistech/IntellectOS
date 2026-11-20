/* Intellect OS M2: bounded slab + virtual page-run heap, no libc/firmware.
 * Slabs: 32, 64, 128, 256, 512, 1024, 2048 bytes.
 * Large runs: up to 64 pages; single CPU, no reentry, IRQs disabled.
 * Physical alias is still identity-RWX: this is NOT global W^X security.
 */
#include <stddef.h>
#include <stdint.h>
#include "kheap64.h"
#include "pmm64.h"
#include "vmm64.h"
#define PAGE 4096u
#define SLOTS INTELLECT_KHEAP64_PAGE_COUNT
#define FIRST INTELLECT_KHEAP64_START_SLOT
#define BIG_MAX INTELLECT_KHEAP64_MAX_LARGE_PAGES
#define CLASSES 7u
enum { FREE=0, SLAB=1, BIG_HEAD=2, BIG_TAIL=3 };
typedef struct {
    uint64_t physical;
    uint64_t used[2];  /* up to 128 blocks per slab */
    uint16_t count;    /* allocated blocks or large-run page count */
    uint8_t kind, class_id;
} meta_t;
static meta_t pages[SLOTS];
static intellect_kheap64_stats_t stats;
static int ready, poison;
static const uint16_t classes[CLASSES]={32u,64u,128u,256u,512u,1024u,2048u};
#ifdef INTELLECT_KHEAP64_HOST_TEST
/* Host tests simulate the high-half alias; they do NOT test hardware PTEs. */
static uint8_t host_window[SLOTS*PAGE] __attribute__((aligned(PAGE)));
#endif
static uintptr_t base_address(void) {
#ifdef INTELLECT_KHEAP64_HOST_TEST
    return (uintptr_t)host_window;
#else
    return (uintptr_t)(INTELLECT_VMM64_BASE+(uint64_t)FIRST*PAGE);
#endif
}
static uint8_t *page_address(unsigned index) {
    return (uint8_t *)(base_address()+(uintptr_t)index*PAGE);
}
static uint16_t slot(unsigned index) {return (uint16_t)(FIRST+index);}
static void scrub(void *ptr,size_t length) {
    volatile uint8_t *p=(volatile uint8_t *)ptr;
    for(size_t i=0;i<length;++i)p[i]=0;
}
static void zero_physical(uint64_t phys) {
    volatile uint64_t *p=(volatile uint64_t *)(uintptr_t)phys;
    for(unsigned i=0;i<PAGE/8u;++i)p[i]=0;
}
static int power_two(size_t v) {return v && !(v&(v-1u));}
static int matches(unsigned i) {
    uint64_t physical=0; int rw=0;
    return intellect_vmm64_query(slot(i),&physical,&rw) &&
        physical==pages[i].physical && rw && intellect_pmm_is_allocated(physical);
}
static int unused(unsigned i) {
    return pages[i].kind==FREE && !intellect_vmm64_query(slot(i),NULL,NULL);
}
static uint64_t attach(unsigned i) {
    if(!unused(i))return 0;
    uint64_t p=intellect_pmm_alloc_page();
    if(!p)return 0;
    zero_physical(p);
    if(!intellect_vmm64_map(slot(i),p,1)) {
        if(!intellect_pmm_free_page(p))poison=1;
        return 0;
    }
    ++stats.mapped_pages;
    if(stats.mapped_pages>stats.peak_mapped_pages)
        stats.peak_mapped_pages=stats.mapped_pages;
    return p;
}
static int detach(unsigned i) {
    if(!matches(i)||!intellect_vmm64_unmap(slot(i)) ||
       !intellect_pmm_free_page(pages[i].physical)) {
        poison=1;return 0;
    }
    pages[i]=(meta_t){0};
    --stats.mapped_pages;
    return 1;
}
int intellect_kheap64_ready(void){return ready&&!poison;}
int intellect_kheap64_init(void){
    if(ready||poison||!intellect_vmm64_ready()||FIRST+SLOTS>INTELLECT_VMM64_SLOTS)
        return 0;
    for(unsigned i=0;i<SLOTS;++i)
        if(intellect_vmm64_query(slot(i),NULL,NULL))return 0;
    for(unsigned i=0;i<SLOTS;++i)pages[i]=(meta_t){0};
    stats=(intellect_kheap64_stats_t){0};
    ready=1;
    return 1;
}
static void *alloc_slab(size_t requested,size_t alignment){
    unsigned cl=0;
    while(cl<CLASSES && (classes[cl]<requested || classes[cl]<alignment))++cl;
    if(cl==CLASSES)return NULL;
    unsigned capacity=PAGE/classes[cl], index=SLOTS;
    for(unsigned i=0;i<SLOTS;++i){
        if(pages[i].kind==SLAB && pages[i].class_id==cl &&
           pages[i].count<capacity){
            if(!matches(i)){poison=1;return NULL;}
            index=i;break;
        }
    }
    if(index==SLOTS){
        for(unsigned i=0;i<SLOTS;++i)
            if(unused(i)){index=i;break;}
        if(index==SLOTS)return NULL;
        uint64_t p=attach(index);
        if(!p)return NULL;
        pages[index].physical=p;
        pages[index].kind=SLAB;
        pages[index].class_id=(uint8_t)cl;
    }
    meta_t *m=&pages[index];
    for(unsigned b=0;b<capacity;++b){
        unsigned word=b/64u;
        uint64_t mask=UINT64_C(1)<<(b%64u);
        if(!(m->used[word]&mask)){
            uint8_t *ptr=page_address(index)+b*classes[cl];
            scrub(ptr,classes[cl]);
            m->used[word]|=mask;
            ++m->count;
            ++stats.live_allocations;
            stats.live_capacity_bytes+=classes[cl];
            return ptr;
        }
    }
    poison=1; /* metadata count and bitmap disagree */
    return NULL;
}
static void *alloc_big(size_t requested,size_t alignment){
    const size_t count=(requested+PAGE-1u)/PAGE;
    if(count==0 || count>BIG_MAX)return NULL;
    for(unsigned first=0;first+count<=SLOTS;++first){
        uintptr_t address=(uintptr_t)page_address(first);
        if(address&(alignment-1u))continue;
        size_t free_count=0;
        for(;free_count<count;++free_count)
            if(!unused(first+(unsigned)free_count))break;
        if(free_count!=count)continue;
        size_t done=0;
        for(;done<count;++done){
            uint64_t phys=attach(first+(unsigned)done);
            if(!phys)break;
            meta_t *m=&pages[first+done];
            m->physical=phys;
            m->kind=BIG_TAIL; /* temporary, for failure rollback */
        }
        if(done<count){
            for(size_t j=0;j<done;++j)
                if(!detach(first+(unsigned)j))return NULL;
            return NULL;
        }
        pages[first].kind=BIG_HEAD;
        pages[first].count=(uint16_t)count;
        ++stats.live_allocations;
        stats.live_capacity_bytes+=(uint64_t)count*PAGE;
        return (void *)address;
    }
    return NULL;
}
void *intellect_kheap64_alloc(size_t size,size_t alignment){
    if(!intellect_kheap64_ready()||!size||size>BIG_MAX*PAGE||
       !power_two(alignment)||alignment>INTELLECT_KHEAP64_MAX_ALIGNMENT)
        return NULL;
    if(size<=2048u && alignment<=2048u)return alloc_slab(size,alignment);
    return alloc_big(size,alignment);
}
int intellect_kheap64_free(void *ptr){
    if(!intellect_kheap64_ready()||!ptr)return 0;
    uintptr_t addr=(uintptr_t)ptr,base=base_address();
    if(addr<base||addr-base>=(uintptr_t)SLOTS*PAGE)return 0;
    unsigned i=(unsigned)((addr-base)/PAGE);
    unsigned offset=(unsigned)((addr-base)%PAGE);
    meta_t *m=&pages[i];
    if(m->kind==SLAB){
        if(m->class_id>=CLASSES){poison=1;return 0;}
        unsigned size=classes[m->class_id];
        if(offset%size)return 0;
        if(!matches(i)){poison=1;return 0;}
        unsigned b=offset/size;
        uint64_t mask=UINT64_C(1)<<(b%64u);
        if(!(m->used[b/64u]&mask))return 0;
        scrub(ptr,size);
        m->used[b/64u]&=~mask;
        --m->count;
        --stats.live_allocations;
        stats.live_capacity_bytes-=size;
        if(!m->count)return detach(i);
        return 1;
    }
    if(m->kind!=BIG_HEAD||offset||!m->count||m->count>BIG_MAX||
       i+m->count>SLOTS)return 0;
    unsigned count=m->count;
    for(unsigned j=0;j<count;++j){
        if(pages[i+j].kind!=(j?BIG_TAIL:BIG_HEAD)||!matches(i+j)){
            poison=1;return 0;
        }
    }
    scrub(ptr,(size_t)count*PAGE);
    for(unsigned j=0;j<count;++j)
        if(!detach(i+j))return 0;
    --stats.live_allocations;
    stats.live_capacity_bytes-=(uint64_t)count*PAGE;
    return 1;
}
intellect_kheap64_stats_t intellect_kheap64_stats(void){return stats;}
int intellect_kheap64_self_test(void){
    if(!intellect_kheap64_ready())return 0;
    intellect_kheap64_stats_t before=intellect_kheap64_stats();
    uint8_t *a=(uint8_t *)intellect_kheap64_alloc(17u,16u);
    uint8_t *b=(uint8_t *)intellect_kheap64_alloc(137u,64u);
    uint8_t *c=(uint8_t *)intellect_kheap64_alloc(8300u,4096u);
    if(!a||!b||!c)return 0;
    if(((uintptr_t)a&15u)||((uintptr_t)b&63u)||((uintptr_t)c&4095u)||
       *a||*b||*c||c[8192u])return 0;
    a[0]=0x5Au;b[136u]=0xA5u;c[0]=0x11u;c[8192u]=0x22u;c[8299u]=0x33u;
    if(a[0]!=0x5Au||b[136u]!=0xA5u||c[8299u]!=0x33u)return 0;
    if(intellect_kheap64_free(a+1)||!intellect_kheap64_free(a)||
       intellect_kheap64_free(a)||!intellect_kheap64_free(b)||
       !intellect_kheap64_free(c))return 0;
    intellect_kheap64_stats_t after=intellect_kheap64_stats();
    return before.mapped_pages==after.mapped_pages &&
           before.live_allocations==after.live_allocations &&
           before.live_capacity_bytes==after.live_capacity_bytes;
}
