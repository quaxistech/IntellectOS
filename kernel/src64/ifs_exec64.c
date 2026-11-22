/* Boot-time, pinned developer IVX loader for the one-file IFS-1 profile.
 * Precondition: IFS-1 volume fully verified by ifs_ro64.
 * A full BLAKE3 pin binds every file byte to the trusted kernel image. The
 * host builder MUST verify Ed25519 before creating the pin. This is NOT a
 * generic Ed25519-verified IVX loader; only this exact executable is allowed.
 */
#include <stdint.h>
#include <stddef.h>
#include "ifs_exec64.h"
#include "ifs_ro64.h"
#include "ifs_blake3_v1.h"
#include "userterminal64.h"
#include "ifs_ivx_pin.h"

#define IVX_FILE_MAX 4096u
static uint8_t file_bytes[IVX_FILE_MAX];
static uint8_t hash[32];
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint64_t le64(const uint8_t *p){return (uint64_t)le32(p)|((uint64_t)le32(p+4)<<32);}
static int eq(const uint8_t *a,const uint8_t *b,size_t n){
    uint8_t diff=0;
    for(size_t i=0;i<n;++i)diff|=a[i]^b[i];
    return diff==0;
}
/* Strictly accept the static developer profile defined in ivx_v1.py.
 * The 16-byte immutable NOP prefix is part of the signed code segment.
 */
int intellect_ifs_exec64_prepare(void){
    size_t len=0;
    if(!intellect_ifs_ro64_file("/sbin/terminal.ivx",file_bytes,sizeof(file_bytes),&len))return 0;
    if(len!=INTELLECT_PINNED_IVX_BYTES || len<360u || len>IVX_FILE_MAX)return 0;
    intellect_ifs_blake3(file_bytes,len,hash);
    if(!eq(hash,intellect_pinned_ivx_blake3,32u))return 0;
    const uint8_t *f=file_bytes;
    static const uint8_t magic[8]={'I','V','X',0,'\r','\n',0x1a,'\n'};
    if(!eq(f,magic,8u) || le16(f+8)!=128u || le16(f+10)!=1u ||
       le16(f+12)!=0u || f[14]!=1u || f[15]!=1u ||
       le32(f+16)!=0u || le32(f+20)!=0u ||
       le16(f+24)!=1u || le16(f+26)!=0u ||
       f[28]!=12u || f[29]!=12u || f[30]!=1u || f[31]!=0u)return 0;
    if(le64(f+32)!=16u || le64(f+48)!=128u ||
       le32(f+56)!=1u || le32(f+60)!=64u ||
       le64(f+64)!=192u || le32(f+72)!=0u || le32(f+76)!=32u)return 0;
    const uint64_t signed_len=le64(f+40);
    const uint64_t merkle_off=le64(f+80);
    const uint64_t merkle_bytes=le64(f+88);
    if(signed_len!=merkle_off || signed_len<209u ||
       signed_len>len || merkle_bytes>len-signed_len ||
       signed_len+merkle_bytes+152u!=len)return 0;
    if(le64(f+128)!=192u || le64(f+144)!=0u ||
       le64(f+160)!=16u || le32(f+168)!=5u ||
       le32(f+172)!=0u || le32(f+176)!=1u ||
       le32(f+180)!=0xffffffffu || le64(f+184)!=0u)return 0;
    const uint64_t bytes=le64(f+136);
    if(bytes<17u || bytes>4096u || le64(f+152)!=bytes ||
       signed_len!=192u+bytes || bytes-16u>4096u)return 0;
    for(unsigned i=0;i<16u;++i)if(f[192u+i]!=0x90u)return 0;
    /* Hash pin authenticates the entire IVX, including Ed25519 envelope.
     * This bounds the temporary trust model: the built-in pin is immutable,
     * an attacker cannot replace or relink a file without changing the EFI.
     */
    return intellect_u1_external_program_set(f+192u+16u,(size_t)bytes-16u);
}
