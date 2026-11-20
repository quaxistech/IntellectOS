/* Deterministic ATA PIO/MBR/GPT fixture; never executes hardware port I/O. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ata_pio64.h"
#include "block64.h"
#include "partition64.h"
#include "diskdiag64.h"
#include "driver64.h"

typedef struct {
    uint8_t sectors[64][512];
    uint16_t identify[256];
    uint32_t lba;
    unsigned pos,command,reads,writes;
    uint8_t addr[4];
    uint8_t error_status;
} fake_t;
static uint8_t in8(void *ctx,uint16_t port) {
    fake_t *d=ctx;
    if(port==0x1F7u||port==0x3F6u) {
        if(d->error_status)return d->error_status;
        if(!d->command) return 0x50u;
        return d->pos==256u?0x50u:0x58u;
    }
    return 0u;
}
static uint16_t in16(void *ctx,uint16_t port) {
    fake_t *d=ctx;
    assert(port==0x1F0u && d->pos<256u && d->command);
    unsigned n=d->pos++;
    if(d->command==0xECu) return d->identify[n];
    assert(d->command==0x20u && d->lba<64u);
    const uint8_t *s=d->sectors[d->lba];
    return (uint16_t)s[2u*n]|(uint16_t)s[2u*n+1u]<<8;
}
static void out8(void *ctx,uint16_t port,uint8_t v) {
    fake_t *d=ctx;
    assert(port!=0x1F0u && port!=0x1F1u && port!=0x3F6u);
    d->writes++;
    if(port==0x1F6u) d->addr[3]=(uint8_t)(v&0xFu);
    else if(port==0x1F3u) d->addr[0]=v;
    else if(port==0x1F4u) d->addr[1]=v;
    else if(port==0x1F5u) d->addr[2]=v;
    else if(port==0x1F2u) assert(v<=1u);
    else if(port==0x1F7u) {
        assert(v==0xECu||v==0x20u);
        d->command=v;d->pos=0;
        if(v==0x20u) {d->lba=(uint32_t)d->addr[0]|(uint32_t)d->addr[1]<<8|
                              (uint32_t)d->addr[2]<<16|(uint32_t)d->addr[3]<<24;
                       d->reads++;}
    } else assert(0);
}
static void le32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(n>>(8u*i));
}
static void le64(uint8_t *p,uint64_t n) {
    le32(p,(uint32_t)n);le32(p+4u,(uint32_t)(n>>32));
}
static char terminal[2048];
static size_t terminal_len;
static void emit(void *ctx,char c) {
    (void)ctx;
    if(terminal_len+1<sizeof(terminal)) terminal[terminal_len++]=c;
    terminal[terminal_len]=0;
}
static void fixture(fake_t *d) {
    memset(d,0,sizeof(*d));
    d->identify[49]=0x0200u;
    d->identify[60]=64u;
    d->identify[106]=0x4000u;
    const char model[41]="IntellectOS TEST ATA 512B DEVICE    ";
    for(unsigned i=0;i<20u;++i) d->identify[27u+i]=
        (uint16_t)(uint8_t)model[2u*i]<<8|(uint8_t)model[2u*i+1u];
    uint8_t *m=d->sectors[0];
    m[446u+4u]=0xEFu;
    le32(m+446u+8u,1u);
    le32(m+446u+12u,63u);
    m[510]=0x55u;m[511]=0xAAu;
    d->sectors[1][0]=0xEBu;d->sectors[1][1]=0x3Cu;
    d->sectors[1][510]=0x55u;d->sectors[1][511]=0xAAu;
}
static int image_read(void *ctx,uint64_t lba,uint8_t buf[512]) {
    fake_t *d=ctx;
    if(lba>=64u) return 0;
    memcpy(buf,d->sectors[lba],512u);return 1;
}
static void create_gpt(fake_t *d) {
    fixture(d);
    uint8_t *m=d->sectors[0];
    m[446u+4u]=0xEEu;
    uint8_t *entries=d->sectors[2];
    entries[0]=1u; /* nonzero type GUID */
    le64(entries+32u,34u);le64(entries+40u,40u);
    uint32_t array_crc=intellect_partition64_crc32(entries,512u);
    uint8_t *h=d->sectors[1];memset(h,0,512u);
    memcpy(h,"EFI PART",8u);le32(h+8u,0x00010000u);
    le32(h+12u,92u);le64(h+24u,1u);le64(h+32u,63u);
    le64(h+40u,34u);le64(h+48u,62u);
    le64(h+72u,2u);le32(h+80u,4u);le32(h+84u,128u);
    le32(h+88u,array_crc);
    le32(h+16u,intellect_partition64_crc32(h,92u));
}
int main(void) {
    static fake_t d;
    fixture(&d);
    intellect_hw64_snapshot_t hw={0};
    hw.pci_cfg1_ready=1u;hw.pci_count=1u;
    hw.pci[0].vendor_id=0x8086u;hw.pci[0].device_id=0x7010u;
    hw.pci[0].class_code=1u;hw.pci[0].subclass=1u;
    assert(intellect_block64_piix_supported(&hw));
    hw.pci[0].prog_if=1u;
    assert(!intellect_block64_piix_supported(&hw));
    hw.pci[0].prog_if=0u;
    hw.pci[0].device_id=0x2922u;
    assert(!intellect_block64_piix_supported(&hw));
    hw.pci[0].device_id=0x7010u;
    hw.pci_truncated=1u;
    assert(!intellect_block64_piix_supported(&hw));
    /* Driver must reserve both I/O ranges BEFORE the first port access,
     * and release both ranges if IDENTIFY or a later probe fails. */
    assert(intellect_driver64_init(&hw));
    assert(intellect_driver64_claim_count()==1u);
    assert(intellect_driver64_begin_pio_ata());
    assert(intellect_driver64_claim_count()==3u);
    assert(intellect_driver64_get(INTELLECT_DRIVER64_ATA_PIO)->state==
           INTELLECT_DRIVER64_PROBING);
    assert(intellect_driver64_finish_pio_ata(0));
    assert(intellect_driver64_claim_count()==1u);
    assert(intellect_driver64_begin_pio_ata());
    assert(intellect_driver64_finish_pio_ata(1));
    assert(intellect_driver64_get(INTELLECT_DRIVER64_ATA_PIO)->state==
           INTELLECT_DRIVER64_ACTIVE);
    assert(!intellect_driver64_begin_pio_ata());
    intellect_ata64_ops_t io={&d,in8,in16,out8};
    intellect_ata64_info_t a;
    assert(intellect_ata64_identify(&io,&a));
    assert(a.sectors==64u);
    uint8_t b[512];
    assert(intellect_ata64_read512(&io,0u,a.sectors,b));
    assert(b[510u]==0x55u&&b[511u]==0xAAu);
    assert(!intellect_ata64_read512(&io,64u,a.sectors,b));
    assert(!intellect_ata64_read512(&io,0x10000000u,a.sectors,b));
    d.error_status=0x51u; /* device reports ERR; never accept stale sector */
    assert(!intellect_ata64_read512(&io,0u,a.sectors,b));
    d.error_status=0u;
    intellect_partition64_mbr_t m;
    assert(intellect_partition64_mbr_parse(b,64u,&m));
    assert(m.entries_valid==1u&&m.entries[0].start_lba==1u&&!m.protective_gpt);
    assert(!intellect_partition64_mbr_parse(b,32u,&m));
    assert(intellect_block64_host_init(&io));
    assert(intellect_block64_info()->ready);
    assert(intellect_block64_read(1u,b));
    assert(b[0]==0xEBu&&b[510u]==0x55u);
    assert(!intellect_block64_read(64u,b));
    assert(!intellect_block64_host_init(&io));
    terminal_len=0;terminal[0]=0;
    assert(intellect_diskdiag64_command("disk info",emit,NULL));
    assert(strstr(terminal,"READONLY=1")!=NULL);
    terminal_len=0;terminal[0]=0;
    assert(intellect_diskdiag64_command("disk mbr",emit,NULL));
    assert(strstr(terminal,"SIGNATURE=55AA")!=NULL);
    terminal_len=0;terminal[0]=0;
    assert(intellect_diskdiag64_command("disk boot",emit,NULL));
    assert(strstr(terminal,"S0 PBR: LBA=1 SIGNATURE=55AA")!=NULL);
    assert(!intellect_diskdiag64_command("disk write",emit,NULL));

    create_gpt(&d);
    intellect_partition64_gpt_t g;
    assert(intellect_partition64_gpt_verify(image_read,&d,64u,&g));
    assert(g.primary_header_valid&&g.entry_array_valid&&g.nonempty_count==1u);
    d.sectors[2][0]^=0x01u;
    assert(!intellect_partition64_gpt_verify(image_read,&d,64u,&g));
    create_gpt(&d);
    /* Damage header checksum, must be rejected even with intact entry array. */
    d.sectors[1][16u]^=0x10u;
    assert(!intellect_partition64_gpt_verify(image_read,&d,64u,&g));
    fixture(&d);
    assert(!intellect_partition64_gpt_verify(image_read,&d,64u,&g));
    printf("PASS: S0 ATA PIO, LBA bounds, MBR, GPT CRC, read-only fake device\n");
    return 0;
}
