/* Isolated Stage E1 framebuffer test (no EFI firmware or QEMU required). */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "../boot/uefi/framebuffer_console.h"

int main(void) {
    uint8_t *staging=calloc(1u,256u*1024u);
    uint32_t *pixels=calloc(640u*480u,sizeof(uint32_t));
    if(!staging || !pixels) abort();
    intellect_bootinfo_t *h=(void *)staging;
    h->magic=INTELLECT_BOOTINFO_MAGIC;h->version=1;
    h->header_size=sizeof(*h);h->total_size=sizeof(*h);
    h->boot_method=INTELLECT_BOOT_UEFI;h->boot_drive=UINT32_MAX;
    h->flags=INTELLECT_FLAG_MEMORY_MAP_VALID;
    h->memory_map_addr=(uintptr_t)(staging+4096u);
    h->memory_map_count=1;h->memory_map_stride=sizeof(intellect_memory_range_t);
    h->video_kind=INTELLECT_VIDEO_FRAMEBUFFER;
    h->pixel_format=INTELLECT_PIXEL_BGRX8888;
    h->framebuffer_addr=(uintptr_t)pixels;
    h->framebuffer_size=640u*480u*4u;
    h->framebuffer_width=640;h->framebuffer_height=480;h->framebuffer_pitch=640u*4u;
    intellect_memory_range_t *ranges=(void *)(staging+4096u);
    ranges[0].base=0x100000u;ranges[0].length=4096u;
    ranges[0].type=INTELLECT_MEMORY_USABLE;
    intellect_uefi_bootinfo_snapshot_t snap={0};
    snap.header=h;snap.ranges=ranges;snap.physical_base=(uintptr_t)staging;
    snap.normalized_count=1;
    if(!intellect_uefi_draw_pre_exit(&snap)) abort();
    if(pixels[0]!=0x102538u || pixels[200u*640u]!=0u)abort();
    h->pixel_format=INTELLECT_PIXEL_BITMASK;
    if(intellect_uefi_draw_pre_exit(&snap))abort();
    puts("PASS: Stage E independent framebuffer drawing + unsupported-format guard");
    free(pixels);free(staging);
    return 0;
}
