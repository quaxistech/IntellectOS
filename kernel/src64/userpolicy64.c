/* Pure bounded-user-range validator: no kernel or hardware dependencies. */
#include <stdint.h>
#include "userterminal64.h"
int intellect_u1_user_range(uint64_t addr,uint64_t count) {
    const uint64_t start=INTELLECT_USER64_BASE;
    const uint64_t size=2u*INTELLECT_USER64_PAGE;
    if(addr<start || addr>start+size) return 0;
    return count<=start+size-addr;
}
