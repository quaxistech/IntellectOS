/* IntellectOS L1 - native frame-buffer splash screen.
 * Original code; relies only on the UEFI GOP framebuffer handed to the kernel.
 * No Boot Services or third-party video driver is linked after ExitBootServices.
 */
#include <stddef.h>
#include <stdint.h>
#include "welcome64.h"
#include "font5x7.h"

typedef struct {
    volatile uint8_t *base;
    uint32_t width, height, pitch, pixel_kind;
} display_t;

/* Color encoded as 0x00RRGGBB. Write no more than four bytes per pixel. */
static void pixel(const display_t *d, uint32_t x, uint32_t y, uint32_t rgb) {
    if (x >= d->width || y >= d->height) return;
    const uint32_t r=(rgb >> 16) & 255u, g=(rgb >> 8) & 255u, b=rgb & 255u;
    volatile uint8_t *p=d->base+(size_t)y*d->pitch+(size_t)x*4u;
    if (d->pixel_kind==INTELLECT_PIXEL_RGBX8888) {
        p[0]=(uint8_t)r;p[1]=(uint8_t)g;p[2]=(uint8_t)b;
    } else {
        p[0]=(uint8_t)b;p[1]=(uint8_t)g;p[2]=(uint8_t)r;
    }
    p[3]=0u;
}
static void rect(const display_t *d, uint32_t x, uint32_t y,
                 uint32_t w, uint32_t h, uint32_t rgb) {
    if (x>=d->width || y>=d->height || !w || !h) return;
    if (w>d->width-x) w=d->width-x;
    if (h>d->height-y) h=d->height-y;
    for(uint32_t j=0;j<h;++j)
        for(uint32_t i=0;i<w;++i) pixel(d,x+i,y+j,rgb);
}
static size_t bounded_length(const char *s, size_t maximum) {
    if (!s) return 0u;
    size_t n=0u;
    while(n<maximum && s[n]) ++n;
    return n;
}
static void glyph(const display_t *d, uint32_t x, uint32_t y,
                  char c, uint32_t scale, uint32_t rgb) {
    for(unsigned row=0;row<7u;++row) {
        const uint8_t bits=intellect_glyph_row(c,row);
        for(unsigned col=0;col<5u;++col) {
            if ((bits & (1u << (4u-col))) != 0u)
                rect(d,x+col*scale,y+row*scale,scale,scale,rgb);
        }
    }
}
static void txt(const display_t *d, uint32_t x, uint32_t y,
                const char *s, size_t max_chars, uint32_t scale, uint32_t rgb) {
    const size_t n=bounded_length(s,max_chars);
    for (size_t i=0;i<n;++i) {
        if (x>=d->width) return;
        glyph(d,x,y,s[i],scale,rgb);
        const uint32_t advance=6u*scale;
        if (advance>d->width-x) return;
        x+=advance;
    }
}
static void number(char out[21], uint64_t v) {
    char reverse[21];size_t n=0u, i=0u;
    do {reverse[n++]=(char)('0'+v%10u);v/=10u;} while(v);
    while(n) out[i++]=reverse[--n];
    out[i]=0;
}
int intellect_welcome64_render(const intellect_bootinfo_t *boot,
                              const intellect_welcome64_info_t *info) {
    if(!boot || !info ||
       boot->video_kind!=INTELLECT_VIDEO_FRAMEBUFFER ||
       (boot->pixel_format!=INTELLECT_PIXEL_BGRX8888 &&
        boot->pixel_format!=INTELLECT_PIXEL_RGBX8888)) return 0;
    /* Physical frame buffer mappings are still provided by Stage E. */
    const uint32_t w=boot->framebuffer_width,h=boot->framebuffer_height;
    const uint32_t pitch=boot->framebuffer_pitch;
    if (w<320u || h<200u || w>8192u || h>8192u ||
        w>UINT32_MAX/4u || pitch<w*4u ||
        boot->framebuffer_addr==0u ||
        (uint64_t)pitch*h>boot->framebuffer_size ||
        boot->framebuffer_addr>UINT64_MAX-boot->framebuffer_size) return 0;
    const display_t d={(volatile uint8_t *)(uintptr_t)boot->framebuffer_addr,
                       w,h,pitch,boot->pixel_format};
    /* Calm high-contrast UI without an allocator, font services or GPU commands. */
    rect(&d,0u,0u,w,h,0x091626);
    rect(&d,0u,0u,w,9u,0x47E0C2);
    rect(&d,0u,9u,w,2u,0x276E73);
    const uint32_t margin=(w>=900u)?72u:22u;
    const uint32_t headline=(w>=1200u)?5u:(w>=700u?4u:2u);
    const uint32_t small=(w>=700u)?2u:1u;
    const uint32_t y_title=(h>=500u)?h/7u:28u;
    const uint32_t title_height=7u*headline;
    rect(&d,margin,y_title-10u,7u,title_height+20u,0x47E0C2);
    txt(&d,margin+20u,y_title,"INTELLECT OS",12u,headline,0xF4F9FF);
    txt(&d,margin+20u,y_title+title_height+20u,
        "NATIVE X86-64 OPERATING SYSTEM",31u,small,0x71DFCE);
    const uint32_t panel_y=y_title+title_height+58u;
    const uint32_t panel_w=(w>2u*margin)?w-2u*margin:w;
    const uint32_t panel_h=(h>panel_y+42u)?h-panel_y-42u:0u;
    rect(&d,margin,panel_y,panel_w,panel_h,0x14263A);
    const uint32_t x=margin+22u;
    uint32_t y=panel_y+13u;
    txt(&d,x,y,"SYSTEM STARTED",14u,small,0x47E0C2);
    y+=(h>=600u)?54u:36u;
    txt(&d,x,y,"UEFI BOOT SERVICES EXITED",25u,small,0xD6E6F2);
    y+=(h>=600u)?43u:27u;
    txt(&d,x,y,"CPU:",4u,small,0x8FAEC5);
    txt(&d,x+6u*small*5u,y,info->cpu_vendor ? info->cpu_vendor : "UNKNOWN",
        12u,small,0xFFFFFF);
    y+=(h>=600u)?40u:26u;
    char value[21];
    number(value,info->pci_functions);
    txt(&d,x,y,"PCI FUNCTIONS:",14u,small,0x8FAEC5);
    txt(&d,x+6u*small*15u,y,value,20u,small,0xFFFFFF);
    y+=(h>=600u)?40u:26u;
    number(value,info->acpi_tables);
    txt(&d,x,y,"ACPI TABLES:",12u,small,0x8FAEC5);
    txt(&d,x+6u*small*13u,y,info->acpi_valid ? value : "UNAVAILABLE",
        11u,small,info->acpi_valid ? 0xFFFFFF : 0xFFBE79);
    y+=(h>=600u)?40u:26u;
    txt(&d,x,y,"STORAGE:",8u,small,0x8FAEC5);
    txt(&d,x+6u*small*9u,y,
        info->storage_readonly_ready ? "READ ONLY" : "NOT INITIALIZED",
        16u,small,info->storage_readonly_ready ? 0x7FF5D7 : 0xFFBE79);
    y+=(h>=600u)?42u:28u;
    if (info->storage_readonly_ready && info->storage_model)
        txt(&d,x,y,info->storage_model,30u,small,0xB3CBE0);
    const uint32_t bottom=(h>30u)?h-30u:0u;
    rect(&d,0u,bottom,w,30u,0x07101C);
    txt(&d,margin,bottom+9u,"TERMINAL EDITION - INSTALLER NOT YET AVAILABLE",
        64u,1u,0x9EC2D1);
    return 1;
}
