#ifndef INTELLECT_PARTITION64_H
#define INTELLECT_PARTITION64_H
#include <stdint.h>
#include <stddef.h>

typedef int (*intellect_partition64_read_t)(void *,uint64_t,uint8_t[512]);
typedef struct {
    uint8_t type;
    uint32_t start_lba;
    uint32_t sector_count;
} intellect_partition64_mbr_entry_t;
typedef struct {
    uint8_t signature_valid;
    uint8_t protective_gpt;
    uint8_t entries_valid;
    intellect_partition64_mbr_entry_t entries[4];
} intellect_partition64_mbr_t;
typedef struct {
    uint64_t first_usable,last_usable;
    uint32_t entry_count,nonempty_count;
    uint8_t primary_header_valid,entry_array_valid;
} intellect_partition64_gpt_t;
/* All storage read callbacks are read-only. A false result never mutates disk. */
int intellect_partition64_mbr_parse(const uint8_t sector[512],uint64_t disk_sectors,
                                   intellect_partition64_mbr_t *out);
/* Bounded GPT primary header+entries parser; backup header is NOT verified.
 * Returns false on absent/invalid primary GPT, and never claims repaired media.
 */
int intellect_partition64_gpt_verify(intellect_partition64_read_t read,void *ctx,
                   uint64_t disk_sectors,intellect_partition64_gpt_t *out);
uint32_t intellect_partition64_crc32(const uint8_t *data,size_t length);
#endif
