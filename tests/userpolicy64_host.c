#include <stdint.h>
#include <stdio.h>
#include "userterminal64.h"
#define A(x) do{if(!(x)){fprintf(stderr,"FAIL line %d\n",__LINE__);return 1;}}while(0)
int main(void){
    const uint64_t a=INTELLECT_USER64_BASE;
    A(intellect_u1_user_range(a,1));
    A(intellect_u1_user_range(a,8192));
    A(intellect_u1_user_range(a+8191,1));
    A(intellect_u1_user_range(a+8192,0));
    A(!intellect_u1_user_range(a+8192,1));
    A(!intellect_u1_user_range(a-1,1));
    A(!intellect_u1_user_range(UINT64_MAX,1));
    A(!intellect_u1_user_range(a+8191,2));
    A(!intellect_u1_user_range(a,UINT64_MAX));
    puts("PASS: U1 user pointer boundary contract");
    return 0;
}
