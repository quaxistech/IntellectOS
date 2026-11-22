#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ifs_ro64.h"
static FILE *disk;
static int read512(void *unused,uint64_t lba,uint8_t out[512]){
    (void)unused;
    if(lba>UINT64_MAX/512u || fseek(disk,(long)(lba*512u),SEEK_SET))return 0;
    return fread(out,1,512,disk)==512;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    disk=fopen(argv[1],"rb");if(!disk)return 3;
    if(fseek(disk,0,SEEK_END))return 4;
    long sz=ftell(disk);if(sz<0 || sz%512!=0)return 5;
    int ok=intellect_ifs_ro64_mount(read512,NULL,(uint64_t)sz/512);
    fprintf(stderr,"C IFS mount = %d\n",ok);
    if(!ok)return 6;
    uint8_t file[4096]={0};size_t n=0;
    if(!intellect_ifs_ro64_file("/etc/timezone",file,sizeof(file),&n))return 7;
    fwrite(file,1,n,stdout);
    return 0;
}
