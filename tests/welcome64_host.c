/* Host guard tests: only the provided framebuffer may be written. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "welcome64.h"
#define W 640u
#define H 480u
#define P (W*4u)
static uint8_t frame[64u+P*H+64u];
static void run(unsigned kind) {
    memset(frame,0xCD,sizeof(frame));
    intellect_bootinfo_t b={0};
    b.video_kind=INTELLECT_VIDEO_FRAMEBUFFER;
    b.pixel_format=kind;
    b.framebuffer_addr=(uintptr_t)(frame+64u);
    b.framebuffer_width=W;b.framebuffer_height=H;
    b.framebuffer_pitch=P;b.framebuffer_size=(uint64_t)P*H;
    const intellect_welcome64_info_t i={"AuthenticAMD",5u,5u,1u,1u,"QEMU HARDDISK",65536u};
    assert(intellect_welcome64_render(&b,&i));
    for(unsigned x=0u;x<64u;++x) {
        assert(frame[x]==0xCD);
        assert(frame[sizeof(frame)-1u-x]==0xCD);
    }
    assert(frame[64u+4u]!=0xCD);
    b.framebuffer_size--;
    assert(!intellect_welcome64_render(&b,&i));
    b.framebuffer_size++;
    b.pixel_format=INTELLECT_PIXEL_UNKNOWN;
    assert(!intellect_welcome64_render(&b,&i));
    b.pixel_format=kind;
    b.framebuffer_pitch=0u;
    assert(!intellect_welcome64_render(&b,&i));
}
int main(void) {
    run(INTELLECT_PIXEL_BGRX8888);
    run(INTELLECT_PIXEL_RGBX8888);
    puts("PASS: welcome renderer valid framebuffer, clipping and rejection");
    return 0;
}
