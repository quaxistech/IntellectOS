/* IntellectOS unified UEFI boot chooser. The UEFI firmware owns the keyboard
 * and GOP until ExitBootServices; none of these protocol pointers reach the OS.
 * No installation, formatting, disk writes, memory allocations or OS drivers.
 */
#include <stddef.h>
#include <stdint.h>
#include "bootmenu.h"
#include "../../kernel/src64/font5x7.h"
#define EFI_NOT_READY (UINT64_C(0x8000000000000000)|6u)
#define KEY_UP 1u
#define KEY_DOWN 2u
#define POLL_US 10000u
#define DEFAULT_POLLS 1200u /* 12 seconds; Enter / 1 starts immediately */

typedef struct {volatile uint8_t *buf;uint32_t w,h,pitch,fmt;} canvas_t;
static void box(const canvas_t *c,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t rgb){
    if(x>=c->w || y>=c->h || !w || !h)return;
    if(w>c->w-x)w=c->w-x;
    if(h>c->h-y)h=c->h-y;
    uint8_t r=(uint8_t)(rgb>>16),g=(uint8_t)(rgb>>8),b=(uint8_t)rgb;
    for(uint32_t yy=0;yy<h;++yy){
        volatile uint8_t *p=c->buf+(size_t)(y+yy)*c->pitch+(size_t)x*4u;
        for(uint32_t xx=0;xx<w;++xx,p+=4){
            p[0]=c->fmt==INTELLECT_PIXEL_RGBX8888?r:b;
            p[1]=g;p[2]=c->fmt==INTELLECT_PIXEL_RGBX8888?b:r;p[3]=0u;
        }
    }
}
static void glyph(const canvas_t *c,uint32_t x,uint32_t y,char ch,uint32_t size,uint32_t rgb){
    for(unsigned row=0;row<7u;++row){
        uint8_t bits=intellect_glyph_row(ch,row);
        for(unsigned k=0;k<5u;++k)if(bits&(1u<<(4u-k)))
            box(c,x+k*size,y+row*size,size,size,rgb);
    }
}
static void line(const canvas_t *c,uint32_t x,uint32_t y,const char *s,
                 unsigned max,uint32_t size,uint32_t rgb){
    for(unsigned n=0;n<max && *s && x<c->w;++n,++s){
        glyph(c,x,y,*s,size,rgb);
        if(6u*size>c->w-x)return;
        x+=6u*size;
    }
}
static void decimal(char out[12],unsigned n){
    char rev[12];unsigned i=0,j=0;
    do{rev[i++]=(char)('0'+(n%10u));n/=10u;}while(n);
    while(i) out[j++]=rev[--i];
    out[j]=0;
}
static void draw(const canvas_t *c,unsigned selected,unsigned drives,unsigned warning){
    const uint32_t w=c->w,h=c->h;
    const uint32_t left=(w>=900u)?w/9u:28u;
    const uint32_t cardw=w-2u*left;
    const uint32_t big=(w>=1100u)?5u:(w>=800u?4u:2u);
    const uint32_t small=(w>=700u)?2u:1u;
    box(c,0u,0u,w,h,0x081422u);
    box(c,0u,0u,w,7u,0x42D7BFu);
    line(c,left, (h>=560u)?h/8u:36u,"INTELLECT OS",12u,big,0xF5F9FFu);
    uint32_t top=(h>=560u)?h/8u+76u:96u;
    line(c,left,top,"BOOT EDITION SELECTION",22u,small,0x8BAABFu);
    top+=((h>=560u)?48u:29u);
    uint32_t ch=(h>=560u)?84u:53u;
    for(unsigned item=0;item<2u;++item){
        uint32_t y=top+item*(ch+13u);
        uint32_t fill= item==selected ? 0x174C56u:0x132638u;
        box(c,left,y,cardw,ch,fill);
        box(c,left,y,item==selected?7u:3u,ch,
            item==selected?0x42D7BFu:0x344C60u);
        line(c,left+20u,y+11u,item==0u?"1  INSTALL TERMINAL EDITION":"2  INSTALL GRAPHICAL EDITION",
             29u,small,item==selected?0xF2FAFFu:0xB4C5D6u);
        if(ch>=75u){
            line(c,left+20u,y+43u,item==0u?"BOOT PREVIEW - INSTALLER COMING NEXT":
                 "NOT AVAILABLE YET",39u,1u,item==0u?0x9EDACDu:0xFFBA96u);
        }
    }
    const uint32_t foot=(h>=115u)?h-115u:0u;
    box(c,left,foot,cardw,2u,0x28465Au);
    line(c,left,foot+17u,warning?"GRAPHICAL EDITION IS NOT READY - CHOOSE 1":
         "ARROWS / 1 / 2 TO SELECT - ENTER TO BOOT",44u,small,
         warning?0xFFBD99u:0xA9CEDBu);
    line(c,left,foot+51u,"STANDARD UEFI KEYBOARD / GOP PREBOOT",36u,1u,0x89ACBCu);
    char value[12];decimal(value,drives);
    line(c,left,foot+67u,"UEFI BLOCK DEVICES:",20u,1u,0x89ACBCu);
    line(c,left+126u,foot+67u,value,11u,1u,0xD6F5FFu);
    line(c,left, h>=23u?h-20u:0u,"DISK WRITE DISABLED - NO INSTALLER OPERATIONS",45u,1u,0x82A9B9u);
}
int intellect_uefi_bootmenu(const intellect_bootinfo_t *boot,
                            intellect_efi_input_t *input,
                            efi_boot_services_t *bs,unsigned drives){
    if(!boot || !input || !input->read_key || !bs || !bs->stall ||
       boot->video_kind!=INTELLECT_VIDEO_FRAMEBUFFER ||
       (boot->pixel_format!=INTELLECT_PIXEL_BGRX8888 &&
        boot->pixel_format!=INTELLECT_PIXEL_RGBX8888))return 0;
    uint32_t w=boot->framebuffer_width,h=boot->framebuffer_height;
    if(w<640u || h<480u || w>8192u || h>8192u ||
       w>UINT32_MAX/4u || boot->framebuffer_pitch<w*4u ||
       boot->framebuffer_addr==0u ||
       (uint64_t)boot->framebuffer_pitch*h>boot->framebuffer_size ||
       boot->framebuffer_addr>UINT64_MAX-boot->framebuffer_size)return 0;
    const canvas_t c={(volatile uint8_t *)(uintptr_t)boot->framebuffer_addr,
                      w,h,boot->framebuffer_pitch,boot->pixel_format};
    unsigned selected=0u,warn=0u,idle=0u;
    draw(&c,selected,drives,warn);
    while(1){
        intellect_efi_key_t key={0,0};
        efi_status_t rc=input->read_key(input,&key);
        if(rc==EFI_SUCCESS){
            idle=0u;
            if(key.unicode_char=='1'){ selected=0u;return 1; }
            if(key.unicode_char=='2'){ selected=1u;warn=1u;draw(&c,selected,drives,warn);continue; }
            if(key.scan_code==KEY_UP){ selected=0u;warn=0u;draw(&c,selected,drives,warn);continue; }
            if(key.scan_code==KEY_DOWN){ selected=1u;warn=0u;draw(&c,selected,drives,warn);continue; }
            if(key.unicode_char==13u){
                if(selected==0u)return 1;
                warn=1u;draw(&c,selected,drives,warn);
            }
        }else if(rc!=EFI_NOT_READY){
            /* Broken keyboard protocol: do not silently boot an installer. */
            return 0;
        }
        /* Timer-based fallback makes QEMU and single-purpose USB boot usable
         * even if nobody presses a key; never performs disk writes. */
        if(++idle>=DEFAULT_POLLS){
            if(selected==0u)return 1;
            idle=0u; /* graphical choice remains blocked until user chooses 1 */
        }
        (void)bs->stall(POLL_US);
    }
}
