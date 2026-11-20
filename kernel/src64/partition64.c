/* S0 read-only MBR/GPT checks, 512-byte LBA; no filesystem mounts/writes.
 * A checksum is integrity detection, not authenticity. Firmware/disk untrusted.
 */
#include <stdint.h>
#include <stddef.h>
#include "partition64.h"
#define GPT_ENTRIES_MAX 128u
#define GPT_ENTRY_BYTES 128u
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
           (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p+4)<<32);
}
static uint32_t crc_update(uint32_t c,const uint8_t *p,size_t n) {
    for(size_t i=0;i<n;++i) {
        c^=p[i];
        for(unsigned j=0;j<8u;++j)
            c=(c>>1)^((uint32_t)(0u-(c&1u))&UINT32_C(0xEDB88320));
    }
    return c;
}
uint32_t intellect_partition64_crc32(const uint8_t *p,size_t n) {
    if(!p && n) return 0u;
    return ~crc_update(UINT32_MAX,p,n);
}
int intellect_partition64_mbr_parse(const uint8_t s[512],uint64_t capacity,
                                   intellect_partition64_mbr_t *out) {
    if(!s || !out || !capacity) return 0;
    *out=(intellect_partition64_mbr_t){0};
    if(s[510u]!=0x55u || s[511u]!=0xAAu) return 0;
    out->signature_valid=1u;
    for(unsigned i=0;i<4u;++i) {
        const uint8_t *e=s+446u+16u*i;
        out->entries[i].type=e[4];
        out->entries[i].start_lba=le32(e+8u);
        out->entries[i].sector_count=le32(e+12u);
        if(e[0]!=0u && e[0]!=0x80u) return 0;
        if(e[4]==0u) {
            if(out->entries[i].start_lba || out->entries[i].sector_count) return 0;
            continue;
        }
        if(!out->entries[i].sector_count || !out->entries[i].start_lba ||
            out->entries[i].start_lba>=capacity ||
            out->entries[i].sector_count>capacity-out->entries[i].start_lba) return 0;
        if(e[4]==0xEEu) out->protective_gpt=1u;
        out->entries_valid++;
    }
    /* Extended partitions and GPT must be treated as containers, not blindly mounted. */
    return 1;
}
int intellect_partition64_gpt_verify(intellect_partition64_read_t read,void *ctx,
                    uint64_t capacity,intellect_partition64_gpt_t *out) {
    if(!out) return 0;
    *out=(intellect_partition64_gpt_t){0};
    if(!read || capacity<35u) return 0;
    uint8_t s[512];
    if(!read(ctx,0u,s)) return 0;
    intellect_partition64_mbr_t m;
    if(!intellect_partition64_mbr_parse(s,capacity,&m) || !m.protective_gpt)
        return 0;
    if(!read(ctx,1u,s)) return 0;
    const uint8_t magic[8]={'E','F','I',' ','P','A','R','T'};
    for(unsigned i=0;i<8u;++i) if(s[i]!=magic[i]) return 0;
    if(le32(s+8u)!=0x00010000u || le32(s+20u)!=0u) return 0;
    uint32_t len=le32(s+12u),expected=le32(s+16u);
    if(len<92u || len>512u) return 0;
    uint8_t hdr[512];for(unsigned i=0;i<len;++i) hdr[i]=s[i];
    for(unsigned i=16u;i<20u;++i) hdr[i]=0u;
    if(intellect_partition64_crc32(hdr,len)!=expected) return 0;
    uint64_t my=le64(s+24u),alternate=le64(s+32u);
    uint64_t first=le64(s+40u),last=le64(s+48u);
    uint64_t entries_lba=le64(s+72u);
    uint32_t count=le32(s+80u),entry_size=le32(s+84u),entry_crc=le32(s+88u);
    if(my!=1u || !alternate || alternate>=capacity || alternate==my ||
       first<2u || last<first || last>=capacity ||
       count==0u || count>GPT_ENTRIES_MAX || entry_size!=GPT_ENTRY_BYTES ||
       entries_lba<2u || entries_lba>=first) return 0;
    uint64_t bytes=(uint64_t)count*entry_size;
    uint64_t sectors=(bytes+511u)/512u;
    if(sectors>first-entries_lba || sectors>capacity-entries_lba) return 0;
    out->primary_header_valid=1u;
    out->first_usable=first;out->last_usable=last;out->entry_count=count;
    uint64_t begin[GPT_ENTRIES_MAX],end[GPT_ENTRIES_MAX];
    unsigned used=0;
    uint32_t c=UINT32_MAX;
    /* Read the CRC-protected entry array and validate live extents. */
    for(uint64_t page=0;page<sectors;++page) {
        if(!read(ctx,entries_lba+page,s)) return 0;
        size_t valid=(size_t)(bytes-page*512u);
        if(valid>512u) valid=512u;
        c=crc_update(c,s,valid);
        for(size_t off=0;off+entry_size<=valid;off+=entry_size) {
            const uint8_t *e=s+off;
            int empty=1;
            for(unsigned i=0;i<16u;++i) if(e[i]) {empty=0;break;}
            if(empty) continue;
            uint64_t lo=le64(e+32u),hi=le64(e+40u);
            if(lo<first || hi>last || hi<lo) return 0;
            for(unsigned j=0;j<used;++j)
                if(lo<=end[j] && begin[j]<=hi) return 0;
            begin[used]=lo;end[used]=hi;
            ++used;
        }
    }
    if(~c!=entry_crc) return 0;
    out->entry_array_valid=1u;
    out->nonempty_count=used;
    return 1;
}
