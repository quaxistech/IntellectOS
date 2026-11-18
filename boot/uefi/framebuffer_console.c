/* Stage E1: own framebuffer text output. Never uses UEFI ConsoleOut.
 * Uses the same original 5x7 glyphs as the independent native kernel.
 */
#include "framebuffer_console.h"
#include "../../kernel/src64/font5x7.h"
static void rect(const intellect_bootinfo_t *bi,uint32_t x,uint32_t y,
                 uint32_t w,uint32_t h,uint32_t rgb) {
    volatile uint8_t *base=(volatile uint8_t *)(uintptr_t)bi->framebuffer_addr;
    uint32_t encoded=bi->pixel_format==INTELLECT_PIXEL_RGBX8888 ?
                    ((rgb&0xFFu)<<16)|(rgb&0xFF00u)|((rgb>>16)&0xFFu):rgb;
    for(uint32_t yy=y;yy<y+h && yy<bi->framebuffer_height;++yy) {
        for(uint32_t xx=x;xx<x+w && xx<bi->framebuffer_width;++xx) {
            volatile uint32_t *target=(volatile uint32_t *)(void *)(base+(size_t)yy*bi->framebuffer_pitch+(size_t)xx*4u);
            *target=encoded;
        }
    }
}
static void glyph(const intellect_bootinfo_t *bi,uint32_t x,uint32_t y,char ch,uint32_t color) {
    for(unsigned row=0;row<7u;++row) {
        uint8_t bits=intellect_glyph_row(ch,row);
        for(unsigned column=0;column<5u;++column)
            if(bits&(1u<<(4u-column)))rect(bi,x+column*2u,y+row*2u,2u,2u,color);
    }
}
int intellect_uefi_draw_pre_exit(const intellect_uefi_bootinfo_snapshot_t *snapshot) {
    if(!intellect_uefi_validate_bootinfo(snapshot))return 0;
    const intellect_bootinfo_t *bi=snapshot->header;
    if(bi->video_kind!=INTELLECT_VIDEO_FRAMEBUFFER ||
       bi->framebuffer_width<640u || bi->framebuffer_height<480u ||
       (bi->pixel_format!=INTELLECT_PIXEL_BGRX8888 && bi->pixel_format!=INTELLECT_PIXEL_RGBX8888))return 0;
    rect(bi,0u,0u,bi->framebuffer_width,74u,0x102538u);
    const char *line="INTELLECT STAGE E - NATIVE HANDOFF";
    uint32_t x=22u;
    while(*line) {glyph(bi,x,22u,*line++,0x8CEAC6u);x+=12u;}
    return 1;
}

void intellect_uefi_draw_failure(const intellect_uefi_bootinfo_snapshot_t *snapshot,
                                  unsigned code) {
    if(!snapshot || !snapshot->header) return;
    const intellect_bootinfo_t *bi=snapshot->header;
    if(bi->video_kind!=INTELLECT_VIDEO_FRAMEBUFFER ||
       bi->framebuffer_width<320u || bi->framebuffer_height<180u ||
       (bi->pixel_format!=INTELLECT_PIXEL_BGRX8888 &&
        bi->pixel_format!=INTELLECT_PIXEL_RGBX8888)) return;
    rect(bi,0u,85u,bi->framebuffer_width,40u,0x552027u);
    const char *line="STAGE E HALTED - ERROR E";
    uint32_t x=22u;
    while(*line) {glyph(bi,x,94u,*line++,0xFFF0E0u);x+=12u;}
    glyph(bi,x,94u,(char)('0'+((code/10u)%10u)),0xFFF0E0u);
    x+=12u;
    glyph(bi,x,94u,(char)('0'+(code%10u)),0xFFF0E0u);
}
