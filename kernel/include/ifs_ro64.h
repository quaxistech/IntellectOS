#ifndef INTELLECT_IFS_RO64_H
#define INTELLECT_IFS_RO64_H
#include <stdint.h>
#include <stddef.h>
/* Read a 512-byte sector: return 1 on success, 0 otherwise. */
typedef int (*intellect_ifs_sector_read_fn)(void *ctx,uint64_t lba,uint8_t out[512]);
/* Single-shard IFS-1 initial BOOT PROFILE, no dynamic allocations, no disk writes. */
int intellect_ifs_ro64_mount(intellect_ifs_sector_read_fn read,void *ctx,uint64_t disk_sectors);
/* 1..4096 bytes in initial profile, *length is set only on success. */
int intellect_ifs_ro64_file(const char *path,uint8_t *out,size_t capacity,size_t *length);
#endif
