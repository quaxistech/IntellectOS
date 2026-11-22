#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ifs_exec64.h"
static uint8_t blob[4096];static size_t n;
static uint8_t executed[4096];static size_t used;
int intellect_ifs_ro64_file(const char *name,uint8_t *out,size_t cap,size_t *len){
    if(strcmp(name,"/sbin/terminal.ivx") || n>cap)return 0;
    memcpy(out,blob,n);*len=n;return 1;
}
int intellect_u1_external_program_set(const uint8_t *code,size_t bytes){
    if(!code || bytes>sizeof(executed) || !bytes)return 0;
    memcpy(executed,code,bytes);used=bytes;return 1;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 3;
    n=fread(blob,1,sizeof(blob),f);fclose(f);
    if(!intellect_ifs_exec64_prepare())return 4;
    if(used==0)return 5;
    blob[100]^=1u;
    if(intellect_ifs_exec64_prepare())return 6;
    printf("PASS: pinned IFS external IVX loader, tamper rejected, executable bytes=%zu\n",used);
    return 0;
}
