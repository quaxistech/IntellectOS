#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "partition_plan_v1.h"
#define MI UINT64_C(1048576)
int main(void) {
    ip_plan_t p;
    /* 16 GiB GPT disk, 512-byte logical sectors, standard 34..(N-34). */
    uint64_t total=UINT64_C(16)*1024u*1024u*1024u/512u;
    assert(ip_init(&p,total,512u,34u,total-34u)==IP_OK);
    assert(ip_default_layout(&p)==IP_OK);
    assert(ip_validate(&p)==IP_OK);
    assert(p.count==2u && p.part[0].first_lba%2048u==0u);
    assert(p.part[0].count_lba==512u*2048u);
    assert(p.part[1].count_lba>=256u*2048u);
    assert(ip_select(&p,p.part[1].id)==IP_OK);
    assert(p.selected_id==p.part[1].id);
    uint32_t other;
    assert(ip_add(&p,IP_PART_DATA,p.part[0].first_lba,100u,&other)==IP_EOVERLAP);
    assert(ip_add(&p,IP_PART_ESP,p.part[0].first_lba,100u,&other)==IP_EBUSY);
    assert(ip_default_layout(&p)==IP_EBUSY);
    uint32_t system_id=p.part[1].id;
    assert(ip_delete(&p,system_id)==IP_OK);
    assert(p.selected_id==IP_NO_SELECTION);
    assert(ip_delete(&p,system_id)==IP_ENOENT);
    assert(ip_validate(&p)==IP_OK);

    /* The minimum-size IntellectFS partition can be planned with small disk. */
    uint64_t small_total=(512u+256u)*MI/512u + 8192u;
    assert(ip_init(&p,small_total,512u,34u,small_total-34u)==IP_OK);
    assert(ip_default_layout(&p)==IP_OK);

    /* Too small must not leave a partial partition on failure. */
    assert(ip_init(&p,512u*MI/512u,512u,34u,512u*MI/512u-34u)==IP_OK);
    assert(ip_default_layout(&p)==IP_ENOSPC && p.count==0u);

    /* Overlaps and integer-wrap are rejected even for arbitrary additions. */
    assert(ip_init(&p,100000u,4096u,6u,99994u)==IP_OK);
    assert(ip_add(&p,IP_PART_DATA,99994u,2u,&other)==IP_ERANGE);
    assert(ip_add(&p,IP_PART_DATA,UINT64_MAX,1u,&other)==IP_ERANGE);
    assert(ip_add(&p,IP_PART_DATA,6u,200u,&other)==IP_OK);
    assert(ip_add(&p,IP_PART_DATA,205u,2u,&other)==IP_EOVERLAP);
    assert(ip_add(&p,IP_PART_DATA,206u,2u,&other)==IP_OK);
    assert(ip_validate(&p)==IP_OK);
    p.part[1].first_lba=205u; /* malformed on-disk representation simulation */
    assert(ip_validate(&p)==IP_EOVERLAP);

    assert(ip_init(NULL,10000u,512u,34u,9900u)==IP_EINVAL);
    puts("PASS: installer partition plan G1 (no physical I/O)");
    return 0;
}
