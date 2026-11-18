/* Native Intellect OS x86-64 Stage E kernel.
 * No firmware services, C runtime, heap, disk access, IO port, or device driver.
 * All graphic writes go to the framebuffer described by BootInfo v1.
 */
#include <stddef.h>
#include <stdint.h>
#include "../include/bootinfo.h"

#include "font5x7.h"

typedef struct {
    volatile uint8_t *base;
    uint32_t width, height, pitch, foreground, background;
    uint32_t col, row, columns, rows;
    uint8_t format;
} console_t;

static void pixel(console_t *c, uint32_t x, uint32_t y, uint32_t rgb) {
    if (x>=c->width || y>=c->height) return;
    uint32_t v=rgb;
    /* RGBX in little endian stores R in lowest byte; BGRX stores B lowest. */
    if (c->format==INTELLECT_PIXEL_RGBX8888)
        v=((rgb & 0xFFu)<<16) | (rgb&0xFF00u) | ((rgb>>16)&0xFFu);
    volatile uint32_t *p=(volatile uint32_t *)(void *)(c->base + (size_t)y*c->pitch + (size_t)x*4u);
    *p=v;
}
static void clear_screen(console_t *c) {
    for (uint32_t y=0; y<c->height; ++y)
      for(uint32_t x=0; x<c->width; ++x) pixel(c,x,y,c->background);
    c->col=0;c->row=0;
}
static void put_char(console_t *c,char ch) {
    if(ch=='\r') return;
    if(ch=='\n') {c->col=0;if(c->row+1<c->rows)++c->row;return;}
    if(c->col>=c->columns) {c->col=0;if(c->row+1<c->rows)++c->row;}
    uint32_t x0=16u+c->col*12u, y0=16u+c->row*16u;
    for(unsigned gy=0;gy<7u;++gy) {
        uint8_t bits=intellect_glyph_row(ch,gy);
        for(unsigned gx=0;gx<5u;++gx) {
            uint32_t color=(bits&(1u<<(4u-gx)))?c->foreground:c->background;
            for(unsigned sy=0;sy<2u;++sy)
                for(unsigned sx=0;sx<2u;++sx)pixel(c,x0+gx*2u+sx,y0+gy*2u+sy,color);
        }
    }
    ++c->col;
}
static void print(console_t *c,const char *s) {while(*s) put_char(c,*s++);}
static void decimal(console_t *c,uint64_t n) {
    char b[21];unsigned i=0;
    do{b[i++]=(char)('0'+n%10u);n/=10u;}while(n);
    while(i)put_char(c,b[--i]);
}
static void hex(console_t *c,uint64_t n) {
    static const char d[]="0123456789ABCDEF";
    print(c,"0x");
    for(int i=60;i>=0;i-=4)put_char(c,d[(n>>i)&15u]);
}
static int good_info(const intellect_bootinfo_t *b) {
    if(!b || b->magic!=INTELLECT_BOOTINFO_MAGIC ||
       b->version!=INTELLECT_BOOTINFO_VERSION || b->header_size!=152u ||
       b->boot_method!=INTELLECT_BOOT_UEFI ||
       b->memory_map_stride!=24u || b->memory_map_count==0 ||
       b->memory_map_count>5300u || b->memory_map_addr==0u ||
       !(b->flags&INTELLECT_FLAG_MEMORY_MAP_VALID) ||
       b->video_kind!=INTELLECT_VIDEO_FRAMEBUFFER ||
       b->framebuffer_addr==0u || b->framebuffer_width<320u ||
       b->framebuffer_height<200u || b->framebuffer_pitch<b->framebuffer_width*4u ||
       b->framebuffer_size<(uint64_t)b->framebuffer_pitch*b->framebuffer_height ||
       (b->pixel_format!=INTELLECT_PIXEL_BGRX8888 && b->pixel_format!=INTELLECT_PIXEL_RGBX8888)) return 0;
    return 1;
}
__attribute__((noreturn)) void intellect64_main(const intellect_bootinfo_t *boot) {
    if(!good_info(boot)) for(;;) __asm__ volatile("cli; hlt");
    console_t c;
    c.base=(volatile uint8_t *)(uintptr_t)boot->framebuffer_addr;
    c.width=boot->framebuffer_width;c.height=boot->framebuffer_height;
    c.pitch=boot->framebuffer_pitch;c.format=(uint8_t)boot->pixel_format;
    c.foreground=0xF2F5FDu;c.background=0x101923u;
    c.columns=(c.width>32u)?(c.width-32u)/12u:0u;
    c.rows=(c.height>32u)?(c.height-32u)/16u:0u;
    if(c.columns<36u || c.rows<16u)for(;;)__asm__ volatile("cli; hlt");
    clear_screen(&c);
    c.foreground=0x71E4BBu;
    print(&c,"INTELLECT OS - NATIVE X86-64 KERNEL\n");
    c.foreground=0xF2F5FDu;
    print(&c,"UEFI STAGE E / INDEPENDENT EXECUTION\n");
    print(&c,"-----------------------------------\n\n");
    print(&c,"EXITBOOTServices: SUCCESS\n");
    print(&c,"UEFI BOOT SERVICES: NOT USED\n");
    print(&c,"KERNEL: POSITION-INDEPENDENT X86-64\n");
    print(&c,"BOOTINFO ABI: V1 VALIDATED\n");
    print(&c,"BOOT METHOD: UEFI\n");
    print(&c,"UEFI REVISION RAW: ");hex(&c,boot->uefi_revision);put_char(&c,'\n');
    print(&c,"FIRMWARE VENDOR: ");print(&c,boot->firmware_vendor);put_char(&c,'\n');
    print(&c,"VIDEO: ");decimal(&c,boot->framebuffer_width);print(&c," X ");decimal(&c,boot->framebuffer_height);
    print(&c," PITCH ");decimal(&c,boot->framebuffer_pitch);put_char(&c,'\n');
    print(&c,"FRAMEBUFFER: ");hex(&c,boot->framebuffer_addr);put_char(&c,'\n');
    print(&c,"MEMORY RANGES: ");decimal(&c,boot->memory_map_count);put_char(&c,'\n');
    print(&c,"ACPI RSDP: ");hex(&c,boot->acpi_rsdp_addr);put_char(&c,'\n');
    print(&c,"SMBIOS: ");hex(&c,boot->smbios_addr);put_char(&c,'\n');
    print(&c,"-----------------------------------\n");
    c.foreground=0x71E4BBu;
    print(&c,"NATIVE KERNEL RUNNING - CPU HALTED\n");
    c.foreground=0xF2F5FDu;
    print(&c,"NO GRUB / NO LINUX / NO DISK WRITES\n");
    for(;;)__asm__ volatile("cli; hlt");
}
