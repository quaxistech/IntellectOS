/* Small independent framebuffer TTY sink. Explicit bounds; no UEFI calls. */
#include <stddef.h>
#include <stdint.h>
#include "terminalfb64.h"
#include "serial64.h"
#include "font5x7.h"
static volatile uint8_t *fb;
static uint32_t width, height, pitch, fmt, xcol, yrow, cols, rows;
static uint32_t top, left;
static int enabled;
static uint32_t pixel32(uint32_t rgb) {
    return fmt==INTELLECT_PIXEL_RGBX8888 ?
      ((rgb&255u)<<16) | (rgb&0xff00u) | ((rgb>>16)&255u) : rgb;
}
static void fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t rgb) {
    if (!enabled || x>=width || y>=height) return;
    if (w>width-x) w=width-x;
    if (h>height-y) h=height-y;
    const uint32_t color=pixel32(rgb);
    for(uint32_t j=0;j<h;++j) {
        volatile uint32_t *p=(volatile uint32_t *)(void *)(fb+(size_t)(y+j)*pitch+(size_t)x*4u);
        for(uint32_t i=0;i<w;++i) p[i]=color;
    }
}
static void glyph(uint32_t x,uint32_t y,char ch) {
    const uint32_t color=pixel32(0xe9f2f7u);
    for(unsigned gy=0;gy<7u;++gy) {
        const uint8_t bits=intellect_glyph_row(ch,gy);
        for(unsigned gx=0;gx<5u;++gx) if(bits & (1u<<(4u-gx))) {
            for(unsigned dy=0;dy<2u;++dy) {
                volatile uint32_t *p=(volatile uint32_t *)(void *)(fb+(size_t)(y+gy*2u+dy)*pitch+(size_t)(x+gx*2u)*4u);
                p[0]=color;p[1]=color;
            }
        }
    }
}
static void label(uint32_t x,uint32_t y,const char *s) {
    while(*s && x+10u<width) {glyph(x,y,*s++);x+=12u;}
}
static void scroll(void) {
    const uint32_t end=top+rows*18u;
    for(uint32_t y=top;y+18u<end;++y) {
        volatile uint32_t *dst=(volatile uint32_t *)(void *)(fb+(size_t)y*pitch+(size_t)left*4u);
        const volatile uint32_t *src=(const volatile uint32_t *)(const void *)(fb+(size_t)(y+18u)*pitch+(size_t)left*4u);
        for(uint32_t x=0;x<cols*12u;++x) dst[x]=src[x];
    }
    fill(left,end-18u,cols*12u,18u,0x111b28u);
}
int intellect_terminalfb64_init(const intellect_bootinfo_t *b) {
    enabled=0;
    if (!b || b->video_kind!=INTELLECT_VIDEO_FRAMEBUFFER ||
        (b->pixel_format!=INTELLECT_PIXEL_RGBX8888 && b->pixel_format!=INTELLECT_PIXEL_BGRX8888) ||
        b->framebuffer_addr==0 || b->framebuffer_width<640u || b->framebuffer_height<480u ||
        b->framebuffer_width>UINT32_MAX/4u || b->framebuffer_pitch<b->framebuffer_width*4u ||
        b->framebuffer_size<(uint64_t)b->framebuffer_pitch*b->framebuffer_height) return 0;
    fb=(volatile uint8_t *)(uintptr_t)b->framebuffer_addr;
    width=b->framebuffer_width;height=b->framebuffer_height;
    pitch=b->framebuffer_pitch;fmt=b->pixel_format;
    left=48u;top=142u;
    cols=(width-96u)/12u;rows=(height-190u)/18u;
    if(cols>100u)cols=100u;
    if(rows>38u)rows=38u;
    if(!cols || !rows)return 0;
    enabled=1;
    intellect_terminalfb64_clear();
    return 1;
}
void intellect_terminalfb64_clear(void) {
    if(!enabled)return;
    fill(0,0,width,height,0x0b1320u);
    fill(0,0,width,104u,0x172d42u);
    fill(0,102u,width,3u,0x38bfa9u);
    label(48u,32u,"INTELLECT OS - TERMINAL EDITION");
    fill(left-12u,top-16u,cols*12u+24u,rows*18u+26u,0x111b28u);
    xcol=0;yrow=0;
}
void intellect_terminalfb64_putc(char c) {
    if(!enabled)return;
    if(c=='\r')return;
    if(c=='\b'){
        if(xcol){--xcol;fill(left+xcol*12u,top+yrow*18u,12u,18u,0x111b28u);}
        return;
    }
    if(c=='\n'){xcol=0;++yrow;}
    else if(c=='\t'){ do {intellect_terminalfb64_putc(' ');} while(xcol%4u);return;}
    else if(c>=32 && c<=126) {
        fill(left+xcol*12u,top+yrow*18u,12u,18u,0x111b28u);
        glyph(left+xcol*12u+1u,top+yrow*18u+2u,c);
        if(++xcol>=cols){xcol=0;++yrow;}
    }
    if(yrow>=rows){scroll();yrow=rows-1u;}
}
void intellect_terminalfb64_write(const char *s,size_t len) {
    for(size_t i=0;i<len;++i) {
        intellect_terminalfb64_putc(s[i]);
        intellect_serial64_putc(s[i]);
    }
}
