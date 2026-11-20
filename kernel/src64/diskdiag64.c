/* S0 safe diagnostics. Only reads first ATA disk; no raw write command exists. */
#include <stddef.h>
#include <stdint.h>
#include "diskdiag64.h"
#include "block64.h"
#include "partition64.h"

typedef struct {intellect_diskdiag64_emit_t fn;void *ctx;} writer_t;
static void ch(writer_t *w,char c){w->fn(w->ctx,c);}
static void txt(writer_t *w,const char *s){while(*s)ch(w,*s++);}
static void dec(writer_t *w,uint64_t n){char s[21];unsigned i=0;
    do{s[i++]=(char)('0'+n%10u);n/=10u;}while(n);
    while(i)ch(w,s[--i]);}
static void hex(writer_t *w,uint32_t n,unsigned digs){
    static const char d[]="0123456789ABCDEF";
    for(unsigned i=digs;i;--i)ch(w,d[(n>>((i-1u)*4u))&15u]);}
static int eq(const char *a,const char *b){
    while(*a&&*b&&*a==*b){++a;++b;}return *a==*b;}
static int rd(void *ctx,uint64_t lba,uint8_t buf[512]){
    (void)ctx;return intellect_block64_read(lba,buf);
}
int intellect_diskdiag64_command(const char *cmd,
                                intellect_diskdiag64_emit_t emit,void *context) {
    if(!cmd || !emit)return 0;
    writer_t w={emit,context};
    if(eq(cmd,"disk")||eq(cmd,"disk help")){
        txt(&w,"DISK COMMANDS: disk info | disk mbr | disk boot | disk gpt\n");
        txt(&w,"READ ONLY: ATA PIO PIIX primary master, no writes/DMA/IRQ\n");return 1;}
    if(!eq(cmd,"disk info")&&!eq(cmd,"disk mbr")&&
       !eq(cmd,"disk boot")&&!eq(cmd,"disk gpt"))return 0;
    const intellect_block64_info_t *info=intellect_block64_info();
    if(!info){txt(&w,"S0 DISK: UNAVAILABLE (no supported PIIX ATA disk)\n");return 1;}
    if(eq(cmd,"disk info")){
        txt(&w,"S0 DISK: READY MODEL=");txt(&w,info->model);
        txt(&w," SECTORS=");dec(&w,info->sectors);
        txt(&w," SECTOR_BYTES=");dec(&w,info->sector_bytes);
        txt(&w," READONLY=1 INTERFACE=ATA_PIO_LBA28\n");return 1;
    }
    uint8_t s[512];
    if(!intellect_block64_read(0u,s)){
        txt(&w,"S0 DISK: READ_ERROR LBA=0\n");return 1;}
    intellect_partition64_mbr_t m;
    if(!intellect_partition64_mbr_parse(s,info->sectors,&m)){
        txt(&w,"S0 MBR: INVALID_OR_UNSUPPORTED\n");return 1;}
    if(eq(cmd,"disk mbr")){
        txt(&w,"S0 MBR: SIGNATURE=55AA PARTITIONS=");dec(&w,m.entries_valid);
        txt(&w," GPT_PROTECTIVE=");dec(&w,m.protective_gpt);ch(&w,'\n');
        for(unsigned i=0;i<4u;++i) if(m.entries[i].type){
            txt(&w,"PART ");dec(&w,i);
            txt(&w," TYPE=0x");hex(&w,m.entries[i].type,2u);
            txt(&w," START=");dec(&w,m.entries[i].start_lba);
            txt(&w," SECTORS=");dec(&w,m.entries[i].sector_count);ch(&w,'\n');
        }
        return 1;
    }
    if(eq(cmd,"disk boot")){
        const intellect_partition64_mbr_entry_t *p=NULL;
        for(unsigned i=0;i<4u;++i) if(m.entries[i].type &&
          m.entries[i].type!=0xEEu && m.entries[i].type!=0x05u &&
          m.entries[i].type!=0x0Fu && m.entries[i].type!=0x85u){p=&m.entries[i];break;}
        if(!p){txt(&w,"S0 PBR: NO_PRIMARY_DATA_PARTITION\n");return 1;}
        if(!intellect_block64_read(p->start_lba,s)){
            txt(&w,"S0 PBR: READ_ERROR\n");return 1;}
        txt(&w,"S0 PBR: LBA=");dec(&w,p->start_lba);
        txt(&w," SIGNATURE=");txt(&w,(s[510]==0x55u&&s[511]==0xAAu)?"55AA":"INVALID");
        txt(&w," FIRST_BYTES=");
        for(unsigned i=0;i<8u;++i)hex(&w,s[i],2u);
        ch(&w,'\n');return 1;
    }
    if(!m.protective_gpt){txt(&w,"S0 GPT: NOT_PRESENT (MBR disk)\n");return 1;}
    intellect_partition64_gpt_t g;
    if(!intellect_partition64_gpt_verify(rd,NULL,info->sectors,&g)){
        txt(&w,"S0 GPT: PRIMARY_INVALID_OR_READ_ERROR\n");return 1;}
    txt(&w,"S0 GPT: PRIMARY_HEADER=VALID ENTRIES_CRC=VALID NONEMPTY=");
    dec(&w,g.nonempty_count);
    txt(&w," BACKUP=NOT_CHECKED\n");return 1;
}
