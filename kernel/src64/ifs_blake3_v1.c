/* IntellectOS portable unkeyed BLAKE3-256.
 * Implements the public BLAKE3 specification. No SIMD assumptions, no heap.
 * This code is not a general-purpose streaming API; memory passed by caller
 * must remain valid for the duration of the calculation.
 */
#include "ifs_blake3_v1.h"
#define CS 1u
#define CE 2u
#define PAR 4u
#define ROOT 8u
static const uint32_t iv[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                              0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
static const unsigned perm[16]={2,6,3,10,7,0,4,13,1,11,12,5,9,14,15,8};
static void store32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(v>>(8u*i));
}
static uint32_t rotr(uint32_t x,unsigned n){return (x>>n)|(x<<(32u-n));}
static void g(uint32_t *s,unsigned a,unsigned b,unsigned c,unsigned d,uint32_t x,uint32_t y){
    s[a]+=s[b]+x;s[d]=rotr(s[d]^s[a],16u);s[c]+=s[d];s[b]=rotr(s[b]^s[c],12u);
    s[a]+=s[b]+y;s[d]=rotr(s[d]^s[a],8u);s[c]+=s[d];s[b]=rotr(s[b]^s[c],7u);
}
static void compress(const uint32_t cv[8],const uint32_t w[16],uint64_t counter,
                     unsigned block_len,unsigned flags,uint32_t result[16]){
    uint32_t s[16],m[16],t[16];
    for(unsigned i=0;i<8u;++i)s[i]=cv[i];
    for(unsigned i=0;i<4u;++i)s[8u+i]=iv[i];
    s[12]=(uint32_t)counter;s[13]=(uint32_t)(counter>>32);
    s[14]=block_len;s[15]=flags;
    for(unsigned i=0;i<16u;++i)m[i]=w[i];
    for(unsigned r=0;r<7u;++r){
        g(s,0,4,8,12,m[0],m[1]);g(s,1,5,9,13,m[2],m[3]);
        g(s,2,6,10,14,m[4],m[5]);g(s,3,7,11,15,m[6],m[7]);
        g(s,0,5,10,15,m[8],m[9]);g(s,1,6,11,12,m[10],m[11]);
        g(s,2,7,8,13,m[12],m[13]);g(s,3,4,9,14,m[14],m[15]);
        for(unsigned i=0;i<16u;++i)t[i]=m[perm[i]];
        for(unsigned i=0;i<16u;++i)m[i]=t[i];
    }
    for(unsigned i=0;i<8u;++i){result[i]=s[i]^s[i+8];result[i+8]=s[i+8]^cv[i];}
}
typedef struct {uint32_t cv[8],words[16];uint64_t counter;unsigned len,flags;} output_t;
static void chain(const output_t *o,uint32_t out[8]) {
    uint32_t v[16];compress(o->cv,o->words,o->counter,o->len,o->flags,v);
    for(unsigned i=0;i<8u;++i)out[i]=v[i];
}
static output_t chunk_output(const uint8_t *p,size_t n,uint64_t idx){
    output_t o;
    uint32_t cv[8];for(unsigned i=0;i<8u;++i)cv[i]=iv[i];
    unsigned blocks = n ? (unsigned)((n+63u)/64u):1u;
    for(unsigned blk=0;blk<blocks;++blk){
        size_t offset=(size_t)blk*64u;
        unsigned len=(unsigned)(n>offset ? ((n-offset)>64u?64u:n-offset):0u);
        for(unsigned i=0;i<8u;++i)o.cv[i]=cv[i];
        for(unsigned i=0;i<16u;++i)o.words[i]=0;
        for(unsigned i=0;i<len;++i)o.words[i/4u]|=(uint32_t)p[offset+i]<<(8u*(i%4u));
        o.counter=idx;o.len=len;o.flags=(blk==0u?CS:0u)|(blk==blocks-1u?CE:0u);
        if(blk!=blocks-1u)chain(&o,cv);
    }
    return o;
}
static output_t parent(const uint32_t left[8],const uint32_t right[8]){
    output_t o;
    for(unsigned i=0;i<8u;++i){o.cv[i]=iv[i];o.words[i]=left[i];o.words[8u+i]=right[i];}
    o.counter=0;o.len=64;o.flags=PAR;
    return o;
}
static output_t subtree(const uint8_t *p,size_t n,uint64_t chunk_index){
    size_t chunks=(n+1023u)/1024u;
    if(chunks<=1u)return chunk_output(p,n,chunk_index);
    size_t left_chunks=1;
    while((left_chunks<<1u)<chunks)left_chunks<<=1u;
    size_t left_size=left_chunks*1024u;
    output_t l=subtree(p,left_size,chunk_index);
    output_t r=subtree(p+left_size,n-left_size,chunk_index+(uint64_t)left_chunks);
    uint32_t lc[8],rc[8];chain(&l,lc);chain(&r,rc);
    return parent(lc,rc);
}
void intellect_ifs_blake3(const uint8_t *data,size_t len,uint8_t out[32]){
    if(out==NULL || (data==NULL && len!=0u))return;
    static const uint8_t empty[1]={0};
    if(data==NULL)data=empty;
    output_t root=subtree(data,len,0);
    uint32_t full[16];compress(root.cv,root.words,0,root.len,root.flags|ROOT,full);
    for(unsigned i=0;i<8u;++i)store32(out+4u*i,full[i]);
}
