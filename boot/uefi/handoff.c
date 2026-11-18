/* UEFI Stage E. Preallocate, identity-map, refresh map, terminate firmware,
 * and jump into a freestanding native x86-64 kernel. No disk operations.
 * Fail-closed: no kernel jump on missing video or invalid memory.
 */
#include "handoff.h"
#include "kernel_embed.h"
#include "framebuffer_console.h"

#define EFI_ALLOCATE_ANY 0u
#define EFI_ALLOCATE_MAX 1u
#define EFI_LOADER_CODE 1u
#define EFI_LOADER_DATA 2u
#define PAGE_SIZE 4096u
#define KERNEL_PAGES 32u
#define STACK_PAGES 16u
#define TABLE_PAGES 8u
#define PAGE_PRESENT_RW UINT64_C(3)
#define PAGE_2M UINT64_C(0x83)
#define PAGE_MMIO_UNCACHED UINT64_C(0x18) /* PWT | PCD for framebuffer */

extern __attribute__((noreturn)) void EFIAPI intellect_efi_jump(uint64_t, uint64_t,
                                                                 intellect_bootinfo_t *, uint64_t);
static uint64_t page_allocate(efi_boot_services_t *bs,uint32_t type,size_t pages) {
    uint64_t address=UINT64_C(0xFFFFFFFF);
    if(EFI_ERROR(bs->allocate_pages(EFI_ALLOCATE_MAX,type,pages,&address))) return 0;
    if(address==0u || address > UINT64_C(0x100000000) - pages*PAGE_SIZE) return 0;
    return address;
}
static void zero_region(void *address,size_t count) {
    uint8_t *b=(uint8_t *)address;
    for(size_t i=0;i<count;++i) b[i]=0;
}
static void copy_region(void *dst,const uint8_t *src,size_t count) {
    uint8_t *d=(uint8_t *)dst;
    for(size_t i=0;i<count;++i) d[i]=src[i];
}

/* Build an independent physical=virtual x86-64 mapping with 2-MiB pages.
 * The first 4 GiB contains kernel, stack, staging, page tables and our
 * tested GOP devices. Two extra 1-GiB windows support GOP at higher addresses
 * below 512 GiB (a bounded Stage E scope, not yet a general memory manager).
 */
static int build_identity_tables(uint64_t tables_addr,uint64_t fb,uint64_t size) {
    if(fb==0u || size==0u || fb>UINT64_MAX-size ||
       fb+size>UINT64_C(512)*1024u*1024u*1024u) return 0;
    uint64_t *pages=(uint64_t *)(uintptr_t)tables_addr;
    zero_region(pages,TABLE_PAGES*PAGE_SIZE);
    uint64_t *pml4=pages;
    uint64_t *pdpt=(uint64_t *)(uintptr_t)(tables_addr+PAGE_SIZE);
    pml4[0]=tables_addr+PAGE_SIZE+PAGE_PRESENT_RW;
    for(uint64_t g=0;g<4u;++g) {
        uint64_t pd= tables_addr + (2u+g)*PAGE_SIZE;
        pdpt[g]=pd+PAGE_PRESENT_RW;
        uint64_t *p=(uint64_t *)(uintptr_t)pd;
        for(uint64_t j=0;j<512u;++j) {
            uint64_t phys=g*UINT64_C(0x40000000)+j*UINT64_C(0x200000);
            uint64_t flags=PAGE_2M;
            if(phys<fb+size && fb<phys+UINT64_C(0x200000)) flags|=PAGE_MMIO_UNCACHED;
            p[j]=phys+flags;
        }
    }
    if(fb+size<=UINT64_C(0x100000000)) return 1;
    uint64_t first=fb/UINT64_C(0x40000000);
    uint64_t last=(fb+size-1u)/UINT64_C(0x40000000);
    uint64_t extra=0;
    for(uint64_t g=first;g<=last;++g) {
        if(g<4u)continue;
        if(extra>=2u || g>=512u) return 0;
        uint64_t pd=tables_addr+(6u+extra)*PAGE_SIZE;
        ++extra;
        pdpt[g]=pd+PAGE_PRESENT_RW;
        uint64_t *p=(uint64_t *)(uintptr_t)pd;
        for(uint64_t j=0;j<512u;++j) {
            uint64_t phys=g*UINT64_C(0x40000000)+j*UINT64_C(0x200000);
            uint64_t flags=PAGE_2M;
            if(phys<fb+size && fb<phys+UINT64_C(0x200000)) flags|=PAGE_MMIO_UNCACHED;
            p[j]=phys+flags;
        }
    }
    return 1;
}
static int check_kernel_image(void) {
    uintptr_t len=(uintptr_t)intellect_kernel64_end-(uintptr_t)intellect_kernel64_start;
    if(len<128u || len>KERNEL_PAGES*PAGE_SIZE) return 0;
    /* Kernel entry occupies the first byte of a flat PIE image. */
    return intellect_kernel64_start[0]!=0u;
}

/* Called after all diagnostics are printed and a framebuffer message is shown.
 * In the final loop ONLY GetMemoryMap and ExitBootServices may be called.
 */
__attribute__((noreturn)) void intellect_uefi_handoff(efi_handle_t image,
        efi_system_table_t *st, intellect_uefi_bootinfo_snapshot_t *snapshot) {
    efi_boot_services_t *bs=st->boot_services;
    unsigned error_code=1u;
    if(!bs || !bs->allocate_pages || !bs->get_memory_map ||
       !bs->exit_boot_services || !snapshot || !snapshot->header ||
       !snapshot->address_below_4g ||
       snapshot->header->video_kind!=INTELLECT_VIDEO_FRAMEBUFFER ||
       (snapshot->header->pixel_format!=INTELLECT_PIXEL_RGBX8888 &&
        snapshot->header->pixel_format!=INTELLECT_PIXEL_BGRX8888) ||
       !check_kernel_image()) goto stop;

    uint64_t kernel=page_allocate(bs,EFI_LOADER_CODE,KERNEL_PAGES);
    uint64_t stack=page_allocate(bs,EFI_LOADER_DATA,STACK_PAGES);
    uint64_t tables=page_allocate(bs,EFI_LOADER_DATA,TABLE_PAGES);
    error_code=2u;
    if(!kernel || !stack || !tables) goto stop;
    zero_region((void *)(uintptr_t)kernel,KERNEL_PAGES*PAGE_SIZE);
    copy_region((void *)(uintptr_t)kernel,intellect_kernel64_start,
                (uintptr_t)intellect_kernel64_end-(uintptr_t)intellect_kernel64_start);
    zero_region((void *)(uintptr_t)stack,STACK_PAGES*PAGE_SIZE);
    error_code=3u;
    if(!build_identity_tables(tables,snapshot->header->framebuffer_addr,
                              snapshot->header->framebuffer_size)) goto stop;

    /* No allocation, output, or other map-invalidating firmware calls follow. */
    for(unsigned attempt=0;attempt<8u;++attempt) {
        size_t bytes=intellect_uefi_final_map_capacity(), key=0, stride=0;
        uint32_t version=0;
        efi_status_t status=bs->get_memory_map(&bytes,
              intellect_uefi_final_map_buffer(snapshot),&key,&stride,&version);
        error_code=4u;
        if(EFI_ERROR(status)) goto stop;
        error_code=5u;
        if(!intellect_uefi_refresh_bootinfo_map(snapshot,bytes,stride,version)) goto stop;
        error_code=6u;
        status=bs->exit_boot_services(image,key);
        if(status==EFI_SUCCESS) {
            intellect_efi_jump(kernel,stack+STACK_PAGES*PAGE_SIZE,
                               snapshot->header,tables);
        }
        if(status!=EFI_INVALID_PARAMETER) goto stop;
    }
stop:
    /* After the first failed ExitBootServices firmware may be partially shut
     * down. Only direct framebuffer writes are used to display the error. */
    intellect_uefi_draw_failure(snapshot,error_code);
    for(;;)__asm__ volatile("pause");
}
