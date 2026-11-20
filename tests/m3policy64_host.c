#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "m3policy64.h"
#define EXPECT(x) do {if(!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1;}}while(0)
int main(void) {
    const uint64_t K=UINT64_C(0x21f000), P=4096;
    intellect_m3_layout_t l={
        .kernel_begin=K, .text_end=K+3*P, .ro_end=K+5*P,
        .image_end=K+80*P, .kernel_reserved_end=K+128*P,
        .stack_guard=UINT64_C(0x810000)
    };
    EXPECT(intellect_m3_layout_valid(&l));
    uint64_t rx=intellect_m3_leaf_for(&l,K,0);
    EXPECT(rx==(K|INTELLECT_M3_PRESENT));
    EXPECT((rx & (INTELLECT_M3_WRITE|INTELLECT_M3_NX|INTELLECT_M3_USER)) == 0);
    uint64_t ro=intellect_m3_leaf_for(&l,l.text_end,0);
    EXPECT(ro==(l.text_end|INTELLECT_M3_PRESENT|INTELLECT_M3_NX));
    uint64_t rw=intellect_m3_leaf_for(&l,l.ro_end,0);
    EXPECT(rw==(l.ro_end|INTELLECT_M3_PRESENT|INTELLECT_M3_WRITE|INTELLECT_M3_NX));
    EXPECT(intellect_m3_leaf_for(&l,l.image_end,0) & INTELLECT_M3_NX);
    EXPECT(intellect_m3_leaf_for(&l,0,0)==(INTELLECT_M3_PRESENT|INTELLECT_M3_WRITE|INTELLECT_M3_NX));
    EXPECT(intellect_m3_leaf_for(&l,l.stack_guard,0)==0);
    EXPECT(intellect_m3_leaf_for(&l,l.stack_guard+P,0) & INTELLECT_M3_NX);
    EXPECT(intellect_m3_leaf_for(&l,0x80000000u,INTELLECT_M3_PCD|INTELLECT_M3_PWT)==
        (UINT64_C(0x80000000)|INTELLECT_M3_PRESENT|INTELLECT_M3_WRITE|
         INTELLECT_M3_NX|INTELLECT_M3_PCD|INTELLECT_M3_PWT));
    EXPECT(intellect_m3_leaf_for(&l,K+1,0)==0);
    EXPECT(intellect_m3_leaf_for(&l,K,INTELLECT_M3_USER)==0);
    EXPECT(intellect_m3_leaf_for(&l,UINT64_C(0x100000000),0)==0);
    l.ro_end=l.text_end-P; EXPECT(!intellect_m3_layout_valid(&l));
    l.ro_end=K+5*P; l.stack_guard=K+4*P;
    EXPECT(!intellect_m3_layout_valid(&l));
    l.stack_guard=0x810000; l.kernel_reserved_end-=P;
    EXPECT(!intellect_m3_layout_valid(&l));
    printf("PASS: M3-A identity leaf policy, NX/WP, guard, invalid layouts\n");
    return 0;
}
