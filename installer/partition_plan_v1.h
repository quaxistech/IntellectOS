#ifndef INTELLECT_INSTALL_PARTITION_PLAN_V1_H
#define INTELLECT_INSTALL_PARTITION_PLAN_V1_H
#include <stddef.h>
#include <stdint.h>

/* Pure, freestanding partition planner. NO disk I/O, NO GPT encoding,
 * NO filesystem formatting, NO implicit writes, NO memory allocation.
 * All LBAs are device-logical-sector indices. No interpretation of existing
 * filesystem or partition contents is performed by this component.
 */
#define IP_MAX_PARTITIONS 128u
#define IP_NO_SELECTION UINT32_MAX

typedef enum {
    IP_OK=0, IP_EINVAL=1, IP_ERANGE=2, IP_EOVERLAP=3,
    IP_EFULL=4, IP_ENOENT=5, IP_EBUSY=6, IP_ENOSPC=7
} ip_status_t;
typedef enum { IP_PART_ESP=1, IP_PART_IFS_SYSTEM=2, IP_PART_DATA=3 } ip_kind_t;
typedef struct {
    uint32_t id;
    uint32_t kind;
    uint64_t first_lba;
    uint64_t count_lba;
    /* Planned removal does NOT erase sectors; execution is a separate phase. */
} ip_part_t;
typedef struct {
    uint64_t disk_sectors;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint32_t logical_sector_bytes;
    uint32_t selected_id;
    uint32_t next_id;
    uint32_t count;
    ip_part_t part[IP_MAX_PARTITIONS];
} ip_plan_t;

ip_status_t ip_init(ip_plan_t *p, uint64_t disk_sectors,
                    uint32_t sector_bytes, uint64_t first_usable,
                    uint64_t last_usable);
ip_status_t ip_add(ip_plan_t *p, uint32_t kind,
                   uint64_t first_lba, uint64_t count_lba, uint32_t *new_id);
ip_status_t ip_delete(ip_plan_t *p, uint32_t id);
ip_status_t ip_select(ip_plan_t *p, uint32_t id);
ip_status_t ip_validate(const ip_plan_t *p);
/* Fresh blank plan only. 512 MiB ESP; IFS >=256 MiB and 1 MiB alignment.
 * No GPT will be written and no disk will be formatted by this API. */
ip_status_t ip_default_layout(ip_plan_t *p);

#endif
