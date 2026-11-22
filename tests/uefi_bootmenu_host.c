/* Host integration: real framebuffer pixels, mocked UEFI keyboard and timers.
 * No BIOS/UEFI calls or physical disks. Run with -O0 and -O2.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "bootmenu.h"

const uint8_t intellect_installed_efi_start[1] = {0};
const uint8_t intellect_installed_efi_end[1] = {0};

#define W 800u
#define H 600u
static uint8_t pixels[W*H*4u];
static intellect_bootinfo_t boot;
static intellect_efi_input_t input;
static efi_boot_services_t services;
static const uint16_t keys[32]={0};
static const uint16_t *sequence;
static size_t seq_size,seq_pos;
static unsigned stalls;
static unsigned input_mode;

static efi_status_t EFIAPI keyboard(struct intellect_efi_input *self,
                                    intellect_efi_key_t *key) {
    (void)self;
    if(input_mode==1u) return UINT64_C(0x8000000000000007);
    if (seq_pos>=seq_size) return UINT64_C(0x8000000000000006);
    uint16_t value=sequence[seq_pos++];
    if(value==0x1001u || value==0x1002u){
        key->scan_code=(uint16_t)(value&255u); key->unicode_char=0;
    } else {key->unicode_char=value;key->scan_code=0;}
    return 0;
}
static efi_status_t EFIAPI stall_uefi(size_t microseconds){
    if(microseconds!=10000u) return UINT64_C(0x8000000000000002);
    ++stalls;return 0;
}
static int test_case(const uint16_t *steps,size_t count,unsigned expected){
    memset(pixels,0,sizeof(pixels));
    sequence=steps;seq_size=count;seq_pos=0;stalls=0;input_mode=0;
    int rc=intellect_uefi_bootmenu(&boot,&input,&services,3u);
    if((unsigned)rc!=expected)return 0;
    if(expected && pixels[0]==0 && pixels[1]==0 && pixels[2]==0)return 0;
    return 1;
}
int main(void){
    memset(&boot,0,sizeof(boot));
    boot.video_kind=INTELLECT_VIDEO_FRAMEBUFFER;
    boot.pixel_format=INTELLECT_PIXEL_BGRX8888;
    boot.framebuffer_addr=(uint64_t)(uintptr_t)pixels;
    boot.framebuffer_size=sizeof(pixels);
    boot.framebuffer_width=W;boot.framebuffer_height=H;
    boot.framebuffer_pitch=W*4u;
    memset(&input,0,sizeof(input));input.read_key=keyboard;
    memset(&services,0,sizeof(services));services.stall=stall_uefi;
    const uint16_t terminal[]={'1'};
    const uint16_t enter[]={13u};
    const uint16_t down_then_enter_then_one[]={0x1002u,13u,0x1001u,13u};
    const uint16_t graphic_then_one[]={'2','1'};
    if(!test_case(terminal,1,1) || !test_case(enter,1,1) ||
       !test_case(down_then_enter_then_one,4,1) ||
       !test_case(graphic_then_one,2,1))return 1;
    /* Invalid / unsafe framebuffer dimensions reject before writing. */
    boot.framebuffer_size=64u;
    if(test_case(terminal,1,0)==0)return 2;
    boot.framebuffer_size=sizeof(pixels);
    input_mode=1u;
    if(intellect_uefi_bootmenu(&boot,&input,&services,0u)!=0)return 3;
    (void)keys;
    puts("PASS: boot menu input, graphical disabled, framebuffer bounds, UEFI errors");
    return 0;
}
