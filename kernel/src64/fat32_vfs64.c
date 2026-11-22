/* IntellectOS minimal GPT -> FAT32 read-only backend. Strict fail-closed.
 * The FAT32 filesystem is ESP-only and not an IntellectFS substitute.
 */
#include <stddef.h>
#include <stdint.h>
#include "fat32_vfs64.h"

static uint16_t rd16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t rd32(const uint8_t *p){return (uint32_t)rd16(p)|((uint32_t)rd16(p+2)<<16);}
static uint64_t rd64(const uint8_t *p){return (uint64_t)rd32(p)|((uint64_t)rd32(p+4)<<32);}
static int eq(const uint8_t *a,const uint8_t *b,size_t n){
    for(size_t i=0;i<n;i++)if(a[i]!=b[i])return 0;
    return 1;
}
static void zero(void *p,size_t n){uint8_t *d=p;for(size_t i=0;i<n;i++)d[i]=0;}
static uint32_t crc32step(uint32_t crc,const uint8_t *b,size_t n){
    for(size_t i=0;i<n;++i){
        crc^=b[i];
        for(unsigned j=0;j<8;++j)crc=(crc>>1)^((crc&1u)?UINT32_C(0xedb88320):0);
    }
    return crc;
}
static int read_sector(const intellect_fat32_mount_t *m,uint64_t lba,uint8_t b[512]){
    return m && m->read && lba<m->disk_sectors && m->read(m->ctx,lba,b);
}
static int in_esp(const intellect_fat32_mount_t *m,uint64_t lba,uint8_t b[512]){
    if(lba<m->esp_lba || lba-m->esp_lba>=m->esp_sectors)return 0;
    return read_sector(m,lba,b);
}
static int p2(uint32_t x){return x && !(x&(x-1u));}
static int entry_cluster(const intellect_fat32_mount_t *m,uint32_t cluster){
    return cluster>=2u && cluster < m->total_clusters+2u;
}
static int fat_next(const intellect_fat32_mount_t *m,uint32_t cluster,uint32_t *out){
    uint8_t sector[512];
    if(!entry_cluster(m,cluster))return 0;
    uint32_t fat_sector=(cluster*4u)/512u;
    if(fat_sector>=m->fat_size || !in_esp(m,m->esp_lba+m->reserved_sectors+fat_sector,sector))return 0;
    *out=rd32(sector+(cluster%128u)*4u)&UINT32_C(0x0fffffff);
    return *out != 0u && *out != 1u && *out != UINT32_C(0x0ffffff7);
}
static int cluster_sector(const intellect_fat32_mount_t *m,uint32_t cluster,
                          unsigned sec,uint64_t *result){
    if(!entry_cluster(m,cluster) || sec>=m->sec_per_cluster)return 0;
    uint64_t delta=(uint64_t)(cluster-2u)*m->sec_per_cluster+sec;
    uint64_t lba=m->data_lba+delta;
    if(lba<m->data_lba || lba>=m->esp_lba+m->esp_sectors)return 0;
    *result=lba;return 1;
}
int intellect_fat32_mount_gpt(intellect_fat32_mount_t *m,
          intellect_fat32_sector_reader_t reader, void *ctx, uint64_t disk_sectors){
    static const uint8_t esp_guid_le[16]={
        0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,
        0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b};
    uint8_t b[512],h[512];
    if(!m || !reader || disk_sectors<2048u+131072u)return 0;
    zero(m,sizeof(*m));m->read=reader;m->ctx=ctx;m->disk_sectors=disk_sectors;
    if(!read_sector(m,0u,b) || b[510]!=0x55u || b[511]!=0xaau || b[450]!=0xeeu)return 0;
    if(!read_sector(m,1u,h) || !eq(h,(const uint8_t *)"EFI PART",8u) ||
       rd32(h+8)!=0x00010000u || rd32(h+12)!=92u || rd64(h+24)!=1u ||
       rd64(h+32)!=disk_sectors-1u || rd32(h+80)!=128u || rd32(h+84)!=128u ||
       rd64(h+72)!=2u || rd64(h+40)!=34u || rd64(h+48)!=disk_sectors-34u)return 0;
    uint32_t stored=rd32(h+16),crc=UINT32_C(0xffffffff);
    for(unsigned i=0;i<92u;++i){
        uint8_t value=(i>=16u && i<20u)?0u:h[i];
        crc=crc32step(crc,&value,1u);
    }
    if((~crc)!=stored)return 0;
    uint32_t entries_crc=UINT32_C(0xffffffff),matched=0;
    for(uint32_t s=0;s<32u;++s){
        if(!read_sector(m,2u+s,b))return 0;
        entries_crc=crc32step(entries_crc,b,512u);
        for(unsigned e=0;e<4u;++e){
            const uint8_t *record=b+e*128u;
            if(!eq(record,esp_guid_le,16u))continue;
            uint64_t first=rd64(record+32),last=rd64(record+40);
            if(++matched!=1u || first<2048u || last<first || last>=disk_sectors-34u)return 0;
            m->esp_lba=first;m->esp_sectors=last-first+1u;
        }
    }
    if((~entries_crc)!=rd32(h+88) || matched!=1u)return 0;
    if(!in_esp(m,m->esp_lba,b) || b[510]!=0x55u || b[511]!=0xaau ||
       rd16(b+11)!=512u || !p2(b[13]) || b[13]>128u || b[16]!=2u ||
       rd16(b+14)<32u || rd16(b+17)!=0u || rd16(b+22)!=0u ||
       rd32(b+36)==0u || rd16(b+40)!=0u || rd16(b+42)!=0u ||
       rd32(b+28)!=m->esp_lba || rd32(b+32)!=m->esp_sectors ||
       !eq(b+82,(const uint8_t *)"FAT32   ",8u))return 0;
    m->reserved_sectors=rd16(b+14);m->sec_per_cluster=b[13];
    m->fat_size=rd32(b+36);m->root_cluster=rd32(b+44);
    uint64_t data_start=m->esp_lba + m->reserved_sectors + 2u*(uint64_t)m->fat_size;
    if(data_start<=m->esp_lba || data_start>=m->esp_lba+m->esp_sectors)return 0;
    m->data_lba=data_start;
    m->total_clusters=(uint32_t)((m->esp_lba+m->esp_sectors-data_start)/m->sec_per_cluster);
    if(m->total_clusters<65525u || m->total_clusters>=0x0ffffff5u ||
       !entry_cluster(m,m->root_cluster) ||
       (uint64_t)m->fat_size*512u/4u < m->total_clusters+2u)return 0;
    m->ready=1;return 1;
}
static int path_component(const char **cursor,uint8_t n[11],int *last){
    const char *s=*cursor;
    unsigned name=0,ext=0,dot=0;
    for(unsigned i=0;i<11;++i)n[i]=' ';
    if(*s=='/' || *s==0)return 0;
    for(;*s && *s!='/';++s){
        char ch=*s;
        if(ch=='.'){
            if(dot || name==0u)return 0;
            dot=1;continue;
        }
        if(ch>='a' && ch<='z')ch=(char)(ch-'a'+'A');
        if(!((ch>='A' && ch<='Z') || (ch>='0' && ch<='9') ||
             ch=='_' || ch=='-'))return 0;
        if(!dot){if(name==8u)return 0;n[name++]=(uint8_t)ch;}
        else{if(ext==3u)return 0;n[8u+ext++]=(uint8_t)ch;}
    }
    if(dot && ext==0u)return 0;
    *last=(*s==0);
    *cursor=(*s=='/')?s+1:s;
    return 1;
}
static int lookup(const intellect_fat32_mount_t *m,uint32_t dir,
                  const uint8_t name[11],intellect_fat32_file_t *result){
    uint8_t b[512];
    for(uint32_t walked=0;walked<m->total_clusters;++walked){
        for(unsigned s=0;s<m->sec_per_cluster;++s){
            uint64_t lba=0;
            if(!cluster_sector(m,dir,s,&lba) || !in_esp(m,lba,b))return 0;
            for(unsigned e=0;e<512u;e+=32u){
                const uint8_t *p=b+e;
                if(p[0]==0)return 0;
                if(p[0]==0xe5u || (p[11]&0x0fu)==0x0fu || (p[11]&0x08u))continue;
                if(eq(p,name,11u)){
                    result->first_cluster=((uint32_t)rd16(p+20)<<16)|rd16(p+26);
                    result->file_size=rd32(p+28);
                    result->is_dir=(p[11]&0x10u)?1u:0u;
                    return entry_cluster(m,result->first_cluster) ||
                           (!result->is_dir && result->file_size==0u);
                }
            }
        }
        uint32_t next=0;
        if(!fat_next(m,dir,&next))return 0;
        if(next>=0x0ffffff8u)return 0;
        dir=next;
    }
    return 0;
}
int intellect_fat32_open(const intellect_fat32_mount_t *m,
          const char *path,intellect_fat32_file_t *out){
    if(!m || !m->ready || !path || !out || path[0]==0)return 0;
    const char *cursor=(path[0]=='/')?path+1:path;
    uint32_t dir=m->root_cluster;
    for(unsigned depth=0;depth<8u;++depth){
        uint8_t component[11];int last=0;
        if(!path_component(&cursor,component,&last))return 0;
        intellect_fat32_file_t file={0};
        if(!lookup(m,dir,component,&file))return 0;
        if(last){*out=file;return 1;}
        if(!file.is_dir)return 0;
        dir=file.first_cluster;
    }
    return 0;
}
int intellect_fat32_read(const intellect_fat32_mount_t *m,
          const intellect_fat32_file_t *f,uint32_t offset,
          uint8_t *out,size_t length,size_t *done){
    if(done)*done=0;
    if(!m || !m->ready || !f || f->is_dir || !done ||
       (!out && length!=0u) || f->file_size>16u*1024u*1024u)return 0;
    if(offset>=f->file_size || length==0u)return 1;
    size_t remaining=f->file_size-offset;
    if(length<remaining)remaining=length;
    size_t needed=remaining;
    const uint32_t cluster_bytes=(uint32_t)m->sec_per_cluster*512u;
    uint32_t skip=offset/cluster_bytes;
    uint32_t cluster=f->first_cluster;
    for(uint32_t walked=0;walked<skip;++walked){
        uint32_t next=0;
        if(!fat_next(m,cluster,&next) || !entry_cluster(m,next))return 0;
        cluster=next;
    }
    uint32_t local=offset%cluster_bytes;
    uint8_t sector[512];
    for(uint32_t walked=0;remaining && walked<m->total_clusters;++walked){
        if(!entry_cluster(m,cluster))return 0;
        while(local<cluster_bytes && remaining){
            uint64_t lba=0;
            if(!cluster_sector(m,cluster,local/512u,&lba) || !in_esp(m,lba,sector))return 0;
            size_t start=local%512u;
            size_t count=512u-start;
            if(count>remaining)count=remaining;
            for(size_t i=0;i<count;++i)out[*done+i]=sector[start+i];
            *done+=count;remaining-=count;local+=(uint32_t)count;
        }
        if(!remaining)break;
        local=0;
        uint32_t next=0;
        if(!fat_next(m,cluster,&next) || !entry_cluster(m,next))return 0;
        cluster=next;
    }
    return *done==needed;
}
