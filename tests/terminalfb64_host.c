#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terminalfb64.h"
static unsigned serial_output;
void intellect_serial64_putc(char c){(void)c;++serial_output;}
#define A(x) do{if(!(x)){fprintf(stderr,"FAIL framebuffer test line %d\n",__LINE__);return 1;}}while(0)
int main(void){
    enum{ W=640,H=480, GUARD=256 };
    const size_t pixels=(size_t)W*H*4u;
    uint8_t *blob=malloc(pixels+2u*GUARD);
    if(!blob)return 2;
    memset(blob,0xA5,pixels+2u*GUARD);
    intellect_bootinfo_t b={0};
    b.video_kind=INTELLECT_VIDEO_FRAMEBUFFER;
    b.framebuffer_addr=(uintptr_t)(blob+GUARD);
    b.framebuffer_width=W;b.framebuffer_height=H;b.framebuffer_pitch=W*4u;
    b.framebuffer_size=pixels;b.pixel_format=INTELLECT_PIXEL_BGRX8888;
    A(intellect_terminalfb64_init(&b));
    for(unsigned i=0;i<20000;++i) intellect_terminalfb64_putc((i%37u)?'A':'\n');
    const char *msg="RUN\n";
    intellect_terminalfb64_write(msg,4u);
    A(serial_output==4u);
    for(unsigned i=0;i<GUARD;++i)A(blob[i]==0xA5 && blob[GUARD+pixels+i]==0xA5);
    b.framebuffer_pitch=(W-1u)*4u;
    A(!intellect_terminalfb64_init(&b));
    free(blob);
    puts("PASS: U1 framebuffer guard regions and invalid geometry");
    return 0;
}
