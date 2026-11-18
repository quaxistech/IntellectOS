#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <sys/mman.h>
#include "../boot/uefi/efi_min.h"
#include "../boot/uefi/handoff.h"
#include "../boot/uefi/bootinfo_builder.h"

/* Real embedded payload is tested via a separate ELF gate; host fixture uses
 * a 1-KiB marker blob to exercise the loader's copy and allocation path. */
__asm__(".pushsection .rodata\n"
        ".global intellect_kernel64_start\n"
        "intellect_kernel64_start: .fill 1024,1,0x90\n"
        ".global intellect_kernel64_end\n"
        "intellect_kernel64_end:\n"
        ".popsection\n");
static jmp_buf done;
static unsigned map_calls, exit_calls, alloc_calls, after_exit_calls;
static uint64_t given_cr3, given_kernel, given_stack;
static intellect_bootinfo_t *given_header;
static int failed_once;
static void *allocate_low(size_t size) {
    void *p=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    if(p==MAP_FAILED || (uintptr_t)p+size > UINT64_C(0x100000000)) abort();
    return p;
}
static efi_status_t EFIAPI fake_allocate(uint32_t alloc_kind,uint32_t memory_type,
                                         size_t pages,uint64_t *address) {
    if(failed_once) ++after_exit_calls;
    if(alloc_kind!=1u || (memory_type!=1u && memory_type!=2u)) abort();
    ++alloc_calls;
    *address=(uint64_t)(uintptr_t)allocate_low(pages*4096u);
    return EFI_SUCCESS;
}
static efi_status_t EFIAPI fake_map(size_t *bytes,void *buffer,size_t *key,
                                     size_t *stride,uint32_t *version) {
    if(*bytes<144u) return EFI_BUFFER_TOO_SMALL;
    ++map_calls;
    uint8_t *p=buffer;
    for(unsigned i=0;i<3u;++i) {
        efi_memory_descriptor_t d={0};
        d.type=i==0u?7u:i==1u?2u:9u;
        d.physical_start=0x100000u+(uint64_t)i*0x200000u;
        d.number_of_pages=i==0u?128u:1u;
        for(size_t j=0;j<48u;++j)p[i*48u+j]=0;
        __builtin_memcpy(p+i*48u,&d,sizeof(d));
    }
    *bytes=144u;*key=map_calls*17u;
    *stride=48u;*version=1u;
    return EFI_SUCCESS;
}
static efi_status_t EFIAPI fake_exit(efi_handle_t image,size_t key) {
    if(image!=(void *)(uintptr_t)0x1234u || key!=map_calls*17u)abort();
    ++exit_calls;
    if(!failed_once) {
        failed_once=1;
        return EFI_INVALID_PARAMETER;
    }
    return EFI_SUCCESS;
}
__attribute__((noreturn)) void EFIAPI intellect_efi_jump(uint64_t kernel,uint64_t stack,
                                   intellect_bootinfo_t *header,uint64_t cr3) {
    if(exit_calls!=2u || map_calls!=2u || alloc_calls!=3u || after_exit_calls!=0u)abort();
    given_kernel=kernel;given_stack=stack;given_header=header;given_cr3=cr3;
    longjmp(done,1);
}
int main(void) {
    efi_boot_services_t bs={0};
    bs.allocate_pages=fake_allocate;
    bs.get_memory_map=fake_map;
    bs.exit_boot_services=fake_exit;
    efi_system_table_t st={0};st.boot_services=&bs;
    uint8_t *mem=allocate_low(64u*4096u);
    intellect_bootinfo_t *header=(intellect_bootinfo_t *)(void *)mem;
    header->magic=INTELLECT_BOOTINFO_MAGIC;
    header->version=INTELLECT_BOOTINFO_VERSION;
    header->header_size=sizeof(*header);
    header->total_size=sizeof(*header);
    header->boot_method=INTELLECT_BOOT_UEFI;
    header->boot_drive=UINT32_MAX;
    header->video_kind=INTELLECT_VIDEO_FRAMEBUFFER;
    header->pixel_format=INTELLECT_PIXEL_BGRX8888;
    header->framebuffer_addr=UINT64_C(0xE0000000);
    header->framebuffer_size=UINT64_C(4)*1024u*1024u;
    header->framebuffer_width=1280u;header->framebuffer_height=800u;header->framebuffer_pitch=5120u;
    header->memory_map_addr=(uintptr_t)(mem+4096u);
    header->memory_map_stride=sizeof(intellect_memory_range_t);
    intellect_uefi_bootinfo_snapshot_t snap={0};
    snap.header=header;snap.ranges=(intellect_memory_range_t *)(void *)(mem+4096u);
    snap.physical_base=(uintptr_t)mem;
    snap.address_below_4g=1u;
    if(setjmp(done)==0) intellect_uefi_handoff((void *)(uintptr_t)0x1234u,&st,&snap);
    if(!given_kernel || !given_stack || !given_header || !given_cr3) abort();
    if(header->memory_map_count!=3u ||
       snap.ranges[0].type!=INTELLECT_MEMORY_USABLE ||
       snap.ranges[1].type!=INTELLECT_MEMORY_RESERVED ||
       snap.ranges[2].type!=INTELLECT_MEMORY_ACPI_RECLAIM) abort();
    uint64_t *pml4=(uint64_t *)(uintptr_t)given_cr3;
    uint64_t *pdpt=(uint64_t *)(uintptr_t)(pml4[0]&~UINT64_C(0xfff));
    if((pml4[0]&3u)!=3u || (pdpt[0]&3u)!=3u || (pdpt[3]&3u)!=3u)abort();
    printf("PASS: Stage E fresh MapKey, stale retry, no post-exit service calls, native jump\n");
    return 0;
}
