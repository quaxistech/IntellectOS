/* Read-only ATA PIO for a known legacy-compatibility PIIX primary channel.
 * Writes are ONLY to ATA command/control registers, never to the data port.
 * Do not invoke concurrently, with interrupts enabled or on an unknown controller.
 */
#include <stddef.h>
#include <stdint.h>
#include "ata_pio64.h"
#define DATA 0x1F0u
#define ERROR 0x1F1u
#define SECCNT 0x1F2u
#define LBA0 0x1F3u
#define LBA1 0x1F4u
#define LBA2 0x1F5u
#define DRIVE 0x1F6u
#define STATUS 0x1F7u
#define ALT 0x3F6u
#define BSY 0x80u
#define DRQ 0x08u
#define DF 0x20u
#define ERR 0x01u

static int valid(const intellect_ata64_ops_t *o) {
    return o && o->in8 && o->in16 && o->out8;
}
static void delay400(const intellect_ata64_ops_t *o) {
    for (unsigned i=0;i<4u;++i) (void)o->in8(o->context,ALT);
}
static int wait_drq(const intellect_ata64_ops_t *o) {
    for (unsigned i=0;i<INTELLECT_ATA64_POLL_LIMIT;++i) {
        uint8_t s=o->in8(o->context,STATUS);
        if(s==0u || s==0xFFu) return 0;
        if(!(s&BSY)) {
            if(s&(ERR|DF)) return 0;
            if(s&DRQ) return 1;
        }
    }
    return 0;
}
static void read_words(const intellect_ata64_ops_t *o, uint16_t w[256]) {
    for (unsigned i=0;i<256u;++i) w[i]=o->in16(o->context,DATA);
}
int intellect_ata64_identify(const intellect_ata64_ops_t *o,intellect_ata64_info_t *out) {
    if(!valid(o) || !out) return 0;
    out->sectors=0u;out->model[0]=0;
    o->out8(o->context,DRIVE,0xA0u);delay400(o);
    o->out8(o->context,SECCNT,0u);
    o->out8(o->context,LBA0,0u);
    o->out8(o->context,LBA1,0u);
    o->out8(o->context,LBA2,0u);
    o->out8(o->context,STATUS,0xECu);delay400(o);
    if(!wait_drq(o)) return 0;
    uint16_t w[256];read_words(o,w);
    /* Reject ATAPI, non-LBA, unsupported sector geometries and empty disks. */
    if((w[0]&0x8000u) || !(w[49]&0x0200u)) return 0;
    if((w[106]&0xD000u)==0x5000u) { /* bit14 valid, bit12 >512-byte logical sector */
        return 0;
    }
    uint32_t count=(uint32_t)w[60]|((uint32_t)w[61]<<16);
    if(!count || count>0x10000000u) return 0;
    out->sectors=count;
    for(unsigned i=0;i<20u;++i) {
        uint16_t v=w[27u+i];
        out->model[2u*i]=(char)(v>>8);
        out->model[2u*i+1u]=(char)v;
    }
    out->model[40]=0;
    for(unsigned i=40u;i && out->model[i-1u]==' ';--i) out->model[i-1u]=0;
    for(unsigned i=0;i<40u && out->model[i];++i)
        if((unsigned char)out->model[i]<32u || (unsigned char)out->model[i]>126u)
            out->model[i]='?';
    return 1;
}
int intellect_ata64_read512(const intellect_ata64_ops_t *o,uint32_t lba,
                             uint32_t capacity, uint8_t out[512]) {
    if(!valid(o) || !out || !capacity || lba>=capacity || lba>=0x10000000u) return 0;
    o->out8(o->context,DRIVE,(uint8_t)(0xE0u|((lba>>24)&15u)));
    delay400(o);
    o->out8(o->context,SECCNT,1u);
    o->out8(o->context,LBA0,(uint8_t)lba);
    o->out8(o->context,LBA1,(uint8_t)(lba>>8));
    o->out8(o->context,LBA2,(uint8_t)(lba>>16));
    o->out8(o->context,STATUS,0x20u);delay400(o);
    if(!wait_drq(o)) return 0;
    for(unsigned i=0;i<256u;++i) {
        uint16_t w=o->in16(o->context,DATA);
        out[2u*i]=(uint8_t)w;
        out[2u*i+1u]=(uint8_t)(w>>8);
    }
    /* A read is successful only when no device error is reported afterwards. */
    uint8_t s=o->in8(o->context,STATUS);
    return s!=0u && s!=0xFFu && !(s&(BSY|ERR|DF|DRQ));
}
