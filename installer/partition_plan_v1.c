#include "partition_plan_v1.h"

static int kind_ok(uint32_t k) {
    return k==IP_PART_ESP || k==IP_PART_IFS_SYSTEM || k==IP_PART_DATA;
}
static int range_ok(const ip_plan_t *p, uint64_t start, uint64_t count) {
    /* Subtraction-based bounds avoid unsigned wrap for arbitrary inputs. */
    return count!=0 && start>=p->first_usable_lba &&
        start<=p->last_usable_lba && count-1u<=p->last_usable_lba-start;
}
static int intersects(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) {
    /* Called only on already-validated, non-overflowing ranges. */
    return a<=b+bn-1u && b<=a+an-1u;
}
static int sector_ok(uint32_t b) {
    return (b==512u || b==1024u || b==2048u || b==4096u);
}
static int find_id(const ip_plan_t *p, uint32_t id, uint32_t *at) {
    for(uint32_t i=0;i<p->count;++i) {
        if(p->part[i].id==id) { if(at)*at=i; return 1; }
    }
    return 0;
}
static int align_up(uint64_t v, uint64_t a, uint64_t *out) {
    if(a==0u || v>UINT64_MAX-(a-1u))return 0;
    *out=((v+a-1u)/a)*a;return 1;
}

ip_status_t ip_init(ip_plan_t *p,uint64_t disk_sectors,uint32_t sector_bytes,
                    uint64_t first_usable,uint64_t last_usable) {
    if(!p || !sector_ok(sector_bytes) || disk_sectors<128u ||
       first_usable==0u || last_usable<first_usable ||
       last_usable>=disk_sectors-1u) return IP_EINVAL;
    *p=(ip_plan_t){0};
    p->disk_sectors=disk_sectors;
    p->logical_sector_bytes=sector_bytes;
    p->first_usable_lba=first_usable;
    p->last_usable_lba=last_usable;
    p->selected_id=IP_NO_SELECTION;
    p->next_id=1u;
    return IP_OK;
}

ip_status_t ip_validate(const ip_plan_t *p) {
    if(!p || !sector_ok(p->logical_sector_bytes) ||
       p->first_usable_lba==0u || p->disk_sectors<128u ||
       p->last_usable_lba<p->first_usable_lba ||
       p->last_usable_lba>=p->disk_sectors-1u || p->count>IP_MAX_PARTITIONS ||
       p->next_id==0u) return IP_EINVAL;
    uint32_t esp=0,ifs=0;
    for(uint32_t i=0;i<p->count;++i) {
        const ip_part_t *a=&p->part[i];
        if(!kind_ok(a->kind) || a->id==0u || a->id>=p->next_id ||
           !range_ok(p,a->first_lba,a->count_lba)) return IP_ERANGE;
        if(a->kind==IP_PART_ESP)++esp;
        if(a->kind==IP_PART_IFS_SYSTEM)++ifs;
        for(uint32_t j=0;j<i;++j) {
            const ip_part_t *b=&p->part[j];
            if(a->id==b->id || intersects(a->first_lba,a->count_lba,
                                            b->first_lba,b->count_lba))
                return IP_EOVERLAP;
        }
    }
    /* This first edition installs one EFI and one system root at most. */
    if(esp>1u || ifs>1u) return IP_EBUSY;
    if(p->selected_id!=IP_NO_SELECTION && !find_id(p,p->selected_id,NULL))
        return IP_ENOENT;
    return IP_OK;
}

ip_status_t ip_add(ip_plan_t *p,uint32_t kind,uint64_t first,uint64_t count,
                   uint32_t *new_id) {
    if(!p || !kind_ok(kind))return IP_EINVAL;
    ip_status_t status=ip_validate(p);
    if(status!=IP_OK)return status;
    if(!range_ok(p,first,count))return IP_ERANGE;
    if(p->count==IP_MAX_PARTITIONS || p->next_id==IP_NO_SELECTION)return IP_EFULL;
    if((kind==IP_PART_ESP || kind==IP_PART_IFS_SYSTEM)) {
        for(uint32_t i=0;i<p->count;++i)
            if(p->part[i].kind==kind)return IP_EBUSY;
    }
    for(uint32_t i=0;i<p->count;++i)
        if(intersects(first,count,p->part[i].first_lba,p->part[i].count_lba))
            return IP_EOVERLAP;
    const uint32_t id=p->next_id++;
    p->part[p->count++]=(ip_part_t){id,kind,first,count};
    if(new_id)*new_id=id;
    return IP_OK;
}

ip_status_t ip_delete(ip_plan_t *p,uint32_t id) {
    if(!p)return IP_EINVAL;
    ip_status_t status=ip_validate(p);
    if(status!=IP_OK)return status;
    uint32_t at;
    if(!find_id(p,id,&at))return IP_ENOENT;
    for(uint32_t i=at+1u;i<p->count;++i)p->part[i-1u]=p->part[i];
    --p->count;
    p->part[p->count]=(ip_part_t){0};
    if(p->selected_id==id)p->selected_id=IP_NO_SELECTION;
    return IP_OK;
}

ip_status_t ip_select(ip_plan_t *p,uint32_t id) {
    if(!p)return IP_EINVAL;
    ip_status_t status=ip_validate(p);
    if(status!=IP_OK)return status;
    if(!find_id(p,id,NULL))return IP_ENOENT;
    p->selected_id=id;
    return IP_OK;
}

ip_status_t ip_default_layout(ip_plan_t *p) {
    if(!p)return IP_EINVAL;
    ip_status_t status=ip_validate(p);
    if(status!=IP_OK)return status;
    if(p->count!=0u)return IP_EBUSY;
    const uint64_t mib=(UINT64_C(1024)*1024u)/p->logical_sector_bytes;
    const uint64_t esp=512u*mib;
    const uint64_t ifs_min=256u*mib;
    uint64_t first;
    if(!align_up(p->first_usable_lba,mib,&first))return IP_ERANGE;
    if(first>p->last_usable_lba || esp-1u>p->last_usable_lba-first)
        return IP_ENOSPC;
    const uint64_t next=first+esp;
    uint64_t ifs_first;
    if(!align_up(next,mib,&ifs_first) || ifs_first>p->last_usable_lba)
        return IP_ENOSPC;
    const uint64_t ifs_count=p->last_usable_lba-ifs_first+1u;
    if(ifs_count<ifs_min)return IP_ENOSPC;
    uint32_t ignored;
    status=ip_add(p,IP_PART_ESP,first,esp,&ignored);
    if(status!=IP_OK)return status;
    status=ip_add(p,IP_PART_IFS_SYSTEM,ifs_first,ifs_count,&ignored);
    if(status!=IP_OK) {
        /* Retain no partial layout on recoverable add failure. */
        p->count=0u;p->part[0]=(ip_part_t){0};p->next_id=1u;
        return status;
    }
    return IP_OK;
}
