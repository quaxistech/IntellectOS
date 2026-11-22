#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "ifs_blake3_v1.h"
int main(void){
    uint8_t d[8192],digest[32];
    const size_t lengths[]={0,1,2,3,4,5,6,63,64,1024,1025,2048,4096};
    for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i){
        for(size_t j=0;j<lengths[i];++j)d[j]=(uint8_t)(j%251u);
        intellect_ifs_blake3(d,lengths[i],digest);
        printf("%zu ",lengths[i]);
        for(unsigned j=0;j<32u;++j)printf("%02x",digest[j]);
        puts("");
    }
    return 0;
}
