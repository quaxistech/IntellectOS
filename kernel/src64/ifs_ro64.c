/* Native IntellectFS-1 initial system volume reader, x86-64 freestanding.
 * Read-only and fail-closed. One GPT partition/volume, four single-level
 * metadata trees, one initial checkpoint seal, one file extent up to 4 KiB.
 * This code DOES NOT implement dynamic CoW writes, Argon2id or IVX signatures.
 */
#include <stdint.h>
#include <stddef.h>
#include "ifs_ro64.h"
#include "ifs_blake3_v1.h"
#define BLK 4096u
#define SECTOR 512u
#define MIN_BLKS UINT64_C(65536)
#define META 256u
#define MAX_DATA 4096u
#define G 0xedb88320u
#define C 0x82f63b78u
static const uint8_t ifs_gpt_guid[16]={0xf1,0x55,0x8f,0xd7,0xf3,0x1c,0xb8,0x56,
                                      0x82,0x2c,0x76,0x8d,0xd5,0x1b,0xa4,0x49};
static const char bdom[]="IFS/bootstrap/v1";
static const char ldom[]="IFS/layout/v1";
static const char sdom[]="IFS/seal/v1";
static const char ddom[]="IFS/commit-desc/v1";
static const char cdom[]="IFS/commit-body/v1";
static const char rdom[]="IFS/root-summary/v1";
static const char pdom[]="IFS/pointer/v1";
static const char ndom[]="IFS/node/v1";
static const uint8_t bootmagic[8]={'I','F','S','B','0','0','0','1'};
static const uint8_t sealmagic[8]={'I','F','S','S','0','0','0','1'};
static const uint8_t nodemagic[8]={'I','F','S','N','0','0','0','1'};
static const uint8_t descmagic[8]={'I','F','S','C','0','0','0','1'};
static const uint8_t layoutmagic[8]={'I','F','S','L','0','0','0','1'};
static uint8_t kblock[BLK],sector[SECTOR],gpt[SECTOR],descriptor[BLK],roots[4][128];
static uint8_t workspace[BLK+160];
static uint8_t data[BLK];
static uint8_t ident[16];
static uint64_t base_lba,volume_blocks,disk_count;
static intellect_ifs_sector_read_fn io;
static void *ioctx;
static uint8_t mounted;
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint64_t le64(const uint8_t *p){return (uint64_t)le32(p)|((uint64_t)le32(p+4)<<32);}
static void cp(uint8_t *dst,const uint8_t *src,size_t n){for(size_t i=0;i<n;++i)dst[i]=src[i];}
static void zero(uint8_t *p,size_t n){for(size_t i=0;i<n;++i)p[i]=0;}
static int eq(const uint8_t *a,const uint8_t *b,size_t n){uint8_t d=0;for(size_t i=0;i<n;++i)d|=a[i]^b[i];return d==0;}
static int z(const uint8_t *b,size_t n){uint8_t q=0;for(size_t i=0;i<n;++i)q|=b[i];return q==0;}
static size_t dom_len(const char *s){size_t n=0;while(s[n])++n;return n+1;}
static uint32_t crc(const uint8_t *p,size_t len,uint32_t poly){
    uint32_t c=0xffffffffu;
    for(size_t i=0;i<len;++i){c^=p[i];for(unsigned k=0;k<8u;++k)c=(c>>1)^((c&1u)?poly:0u);}
    return ~c;
}
static uint32_t crc_update(uint32_t c,const uint8_t *p,size_t len,uint32_t poly){
    for(size_t i=0;i<len;++i){c^=p[i];for(unsigned k=0;k<8u;++k)c=(c>>1)^((c&1u)?poly:0u);}
    return c;
}
static int check_hash(const char *domain,const uint8_t *p,size_t count,size_t masked,size_t masklen,const uint8_t expected[32]){
    size_t d=dom_len(domain);
    if(count>BLK || masked>count || masklen>count-masked || d+count>sizeof(workspace))return 0;
    cp(workspace,(const uint8_t *)domain,d);cp(workspace+d,p,count);
    zero(workspace+d+masked,masklen);
    uint8_t hash[32];intellect_ifs_blake3(workspace,d+count,hash);
    return eq(hash,expected,32);
}
static int physical(uint64_t block,uint8_t out[BLK]){
    if(block>=volume_blocks || block>UINT64_MAX/8u || base_lba>UINT64_MAX-block*8u-8u)return 0;
    uint64_t sector_lba=base_lba+block*8u;
    if(sector_lba>disk_count-8u)return 0;
    for(unsigned j=0;j<8u;++j)if(!io(ioctx,sector_lba+j,out+j*512u))return 0;
    return 1;
}
static int gpt_check_header(uint64_t lba,uint64_t peer,uint64_t entries_lba,
                            uint32_t *entry_crc){
    if(!io(ioctx,lba,sector))return 0;
    if(!eq(sector,(const uint8_t *)"EFI PART",8) || le32(sector+8)!=0x10000u ||
       le32(sector+12)!=92 || le64(sector+24)!=lba || le64(sector+32)!=peer ||
       le64(sector+72)!=entries_lba || le32(sector+80)!=128 || le32(sector+84)!=128)return 0;
    uint32_t checksum=le32(sector+16);
    cp(gpt,sector,SECTOR);zero(gpt+16,4);
    if(checksum!=crc(gpt,92,G))return 0;
    *entry_crc=le32(sector+88);
    return 1;
}
static int gpt_entries(uint64_t entries_lba,uint32_t expect,int parse){
    uint32_t crcacc=0xffffffffu;
    uint64_t start=0,last=0;
    for(unsigned n=0;n<32u;++n){
        if(entries_lba+n>=disk_count || !io(ioctx,entries_lba+n,sector))return 0;
        crcacc=crc_update(crcacc,sector,SECTOR,G);
        if(n==0u){
            uint8_t *p=sector+128;
            if(!eq(p,ifs_gpt_guid,16))return 0;
            start=le64(p+32);last=le64(p+40);
            if(start<2048u || last<=start || last>=disk_count-33u || start%8u!=0)return 0;
            if(parse){base_lba=start;volume_blocks=(last-start+1u)/8u;}
        }
    }
    if(~crcacc!=expect)return 0;
    return (!parse || volume_blocks>=MIN_BLKS);
}
static int ptr_load(const uint8_t ptr[128],unsigned kind,uint8_t out[BLK],uint32_t *len){
    uint64_t pblk=le64(ptr),blocks=le32(ptr+8),stored=le32(ptr+12);
    if(pblk<META || blocks!=1u || stored==0 || stored>BLK ||
       pblk>=volume_blocks-3 || le32(ptr+16)!=stored || !z(ptr+20,8) ||
       le64(ptr+28)!=1 || !z(ptr+52,16) || le16(ptr+68)!=1 ||
       le16(ptr+70)!=kind || !z(ptr+104,24))return 0;
    if(!physical(pblk,out) || !z(out+stored,BLK-stored))return 0;
    size_t d=dom_len(pdom);
    if(d+16+72+16+stored>sizeof(workspace))return 0;
    cp(workspace,(const uint8_t *)pdom,d);cp(workspace+d,ident,16);
    cp(workspace+d+16,ptr,72);cp(workspace+d+88,ptr+104,16);
    cp(workspace+d+104,out,stored);
    uint8_t hash[32];intellect_ifs_blake3(workspace,d+104+stored,hash);
    if(!eq(hash,ptr+72,32))return 0;
    *len=stored;return 1;
}
static int record_find(const uint8_t node[BLK],unsigned kind,const uint8_t *key,
                       size_t klen,const uint8_t **value,size_t *vlen){
    if(!eq(node,nodemagic,8)||le16(node+8)!=1||le16(node+10)!=kind||
       le16(node+12)!=96 || node[14]!=0 || node[15]!=0 ||
       !z(node+16,16) || le64(node+32)!=1 || le64(node+48)!=0 ||
       le64(node+56)!=0 || !check_hash(ndom,node,BLK,64,32,node+64))return 0;
    uint32_t count=le32(node+40),size=le32(node+44);
    if(size>4000u || count>444u)return 0;
    size_t off=96,end=96u+size;
    const uint8_t *prior=NULL;size_t priorlen=0;
    *value=NULL;*vlen=0;
    for(uint32_t i=0;i<count;++i){
        if(off+8u>end)return 0;
        uint16_t nk=le16(node+off),nv=le16(node+off+2),type=le16(node+off+4),flags=le16(node+off+6);
        off+=8;
        if(!nk || type!=kind || flags || nk+nv>end-off)return 0;
        const uint8_t *k=node+off;off+=nk;
        const uint8_t *v=node+off;off+=nv;
        if(prior){
            unsigned cmp=0;size_t m=priorlen<nk?priorlen:nk;
            for(size_t j=0;j<m;++j){if(prior[j]!=k[j]){cmp=prior[j]<k[j]?1u:2u;break;}}
            if((cmp==0 && priorlen>=nk) || cmp==2u)return 0;
        }
        prior=k;priorlen=nk;
        if(nk==klen && eq(k,key,klen)){*value=v;*vlen=nv;}
    }
    return off==end && z(node+end,BLK-end);
}
static int root_read(unsigned root,unsigned kind){
    if(root>=4u)return 0;
    uint32_t sz=0;
    return ptr_load(roots[root],2,kblock,&sz) && sz==BLK && le16(kblock+10)==kind;
}
int intellect_ifs_ro64_mount(intellect_ifs_sector_read_fn read,void *ctx,uint64_t disk_sectors){
    mounted=0;io=read;ioctx=ctx;disk_count=disk_sectors;
    if(!io || disk_count<2097152u)return 0;
    uint32_t e1=0,e2=0;
    if(!gpt_check_header(1,disk_count-1,2,&e1) || !gpt_entries(2,e1,1))return 0;
    uint64_t s1=base_lba,n1=volume_blocks;
    if(!gpt_check_header(disk_count-1,1,disk_count-33,&e2) ||
       !gpt_entries(disk_count-33,e2,1) || e2!=e1 || s1!=base_lba || n1!=volume_blocks)return 0;
    uint8_t valid=0;uint8_t uid[16];
    const uint64_t positions[4]={1,2,volume_blocks-3,volume_blocks-2};
    for(unsigned i=0;i<4u;++i){
        if(!physical(positions[i],kblock))return 0;
        if(!eq(kblock,bootmagic,8) || le16(kblock+8)!=1 || le16(kblock+10)!=0 ||
           le16(kblock+12)!=256 || le16(kblock+14)!=12 ||
           le64(kblock+32)!=volume_blocks || le64(kblock+48)!=1 || le64(kblock+56)!=1 ||
           le64(kblock+64)!=3 || le32(kblock+72)!=16 || le32(kblock+76)!=1 ||
           le64(kblock+80)!=19 || le64(kblock+88)!=64 || le64(kblock+96)!=19 ||
           !z(kblock+104,12) || le32(kblock+116)!=512 || le32(kblock+120)!=1024 ||
           le32(kblock+124)!=1 || !z(kblock+160,BLK-160) ||
           !check_hash(bdom,kblock,BLK,128,32,kblock+128))continue;
        if(!valid)cp(uid,kblock+16,16);
        else if(!eq(uid,kblock+16,16))return 0;
        valid=1;
    }
    if(!valid)return 0;
    cp(ident,uid,16);
    if(!physical(20,descriptor) || !eq(descriptor,descmagic,8) ||
       le16(descriptor+8)!=1 || le16(descriptor+10)!=128 ||
       le64(descriptor+16)!=1 || le64(descriptor+24)!=1 || le64(descriptor+32)!=1 ||
       !eq(descriptor+40,ident,16) || !z(descriptor+88,40))return 0;
    uint32_t total=le32(descriptor+12);
    if(total<128u || total>BLK || !z(descriptor+total,BLK-total))return 0;
    if(!check_hash(cdom,descriptor+128,total-128,total-128,0,descriptor+56))return 0;
    uint8_t root_set[520];uint8_t found=0;size_t off=128;unsigned tag_expected=1;
    while(off<total){
        if(off+8>total)return 0;
        unsigned tag=le16(descriptor+off),flags=le16(descriptor+off+2);
        uint32_t length=le32(descriptor+off+4);off+=8;
        if(tag!=tag_expected++ || flags || length>total-off)return 0;
        if(tag==1u){
            if(length!=520u || le32(descriptor+off)!=0 || le32(descriptor+off+4)!=4)return 0;
            cp(root_set,descriptor+off,520);
            for(unsigned j=0;j<4u;++j)cp(roots[j],root_set+8u+j*128u,128u);
            found=1;
        }
        if(tag==11u){
            if(length!=40u || le64(descriptor+off)!=19 || !physical(19,kblock) ||
               !eq(kblock,layoutmagic,8) || le16(kblock+8)!=1 || le16(kblock+10)!=128 ||
               !eq(kblock+16,ident,16) || !check_hash(ldom,kblock,BLK,96,32,kblock+96))return 0;
            uint8_t hash[32];intellect_ifs_blake3(kblock,BLK,hash);
            if(!eq(hash,descriptor+off+8,32))return 0;
        }
        off+=length;
    }
    if(tag_expected!=12u || !found || !physical(3,kblock) || !eq(kblock,sealmagic,8)||
       le16(kblock+8)!=1 || le16(kblock+10)!=256 || kblock[12]!=2 || kblock[13]!=2 ||
       !eq(kblock+16,ident,16) || le64(kblock+32)!=1 || le32(kblock+40)!=0xffffffffu ||
       le64(kblock+48)!=1 || le64(kblock+56)!=0 || le64(kblock+64)!=1 ||
       le64(kblock+72)!=1 || le64(kblock+80)!=20 || le32(kblock+88)!=1 ||
       le32(kblock+92)!=1 || le64(kblock+200)!=3 || le32(kblock+212)!=1 ||
       !z(kblock+128,32) || !z(kblock+248,BLK-248))return 0;
    uint8_t hash[32];
    size_t domain=dom_len(ddom);cp(workspace,(const uint8_t *)ddom,domain);cp(workspace+domain,descriptor,BLK);
    intellect_ifs_blake3(workspace,domain+BLK,hash);
    if(!eq(hash,kblock+96,32))return 0;
    domain=dom_len(rdom);cp(workspace,(const uint8_t *)rdom,domain);cp(workspace+domain,root_set,520);
    intellect_ifs_blake3(workspace,domain+520,hash);
    if(!eq(hash,kblock+160,32))return 0;
    uint32_t sealcrc=le32(kblock+208);
    cp(workspace,kblock,BLK);zero(workspace+208,4);
    if(crc(workspace,BLK,C)!=sealcrc)return 0;
    domain=dom_len(sdom);cp(workspace,(const uint8_t *)sdom,domain);
    cp(workspace+domain,kblock,BLK);zero(workspace+domain+208,4);zero(workspace+domain+216,32);
    intellect_ifs_blake3(workspace,domain+BLK,hash);
    if(!eq(hash,kblock+216,32))return 0;
    for(unsigned k=0;k<4u;++k){
        if(!root_read(k,k+1u))return 0;
        const uint8_t *val=NULL;size_t nv=0;
        if(!record_find(kblock,k+1u,(const uint8_t *)"\0",1u,&val,&nv))return 0;
    }
    mounted=1;return 1;
}
int intellect_ifs_ro64_file(const char *path,uint8_t *out,size_t capacity,size_t *length){
    if(!mounted||!path||!out||!length||path[0]!='/' || path[1]==0)return 0;
    uint8_t parent[16]={0};parent[0]=1;parent[8]=1;
    size_t off=1;
    while(path[off]){
        size_t start=off;
        while(path[off]&&path[off]!='/')++off;
        size_t part=off-start;
        if(part==0 || part>255u || (part==1 && path[start]=='.') ||
           (part==2 && path[start]=='.'&&path[start+1]=='.'))return 0;
        uint8_t key[271];cp(key,parent,16);
        for(size_t i=0;i<part;++i)key[16+i]=(uint8_t)path[start+i];
        if(!root_read(1,2))return 0;
        const uint8_t *v=NULL;size_t n=0;
        if(!record_find(kblock,2,key,16+part,&v,&n) || !v || n!=16)return 0;
        cp(parent,v,16);
        if(path[off]=='/'){++off;if(!path[off])return 0;}
    }
    if(!root_read(0,1))return 0;
    const uint8_t *v=NULL;size_t n=0;
    if(!record_find(kblock,1,parent,16,&v,&n)||!v||n!=144||v[0]!=2)return 0;
    uint64_t size=le64(v+24);
    if(size>MAX_DATA || size>capacity)return 0;
    if(size==0){*length=0;return 1;}
    uint8_t key[24];cp(key,parent,16);zero(key+16,8);
    if(!root_read(2,3))return 0;
    if(!record_find(kblock,3,key,24,&v,&n)||!v||n!=128)return 0;
    uint32_t stored=0;
    if(!ptr_load(v,1,data,&stored)||stored!=size)return 0;
    cp(out,data,stored);*length=stored;return 1;
}
