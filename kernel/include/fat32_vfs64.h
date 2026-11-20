#ifndef INTELLECT_FAT32_VFS64_H
#define INTELLECT_FAT32_VFS64_H
#include <stddef.h>
#include <stdint.h>
/* Read-only disk / ESP backend for later IntellectOS VFS integration.
 * Sector callback may forward to existing intellect_block64_read.
 * No heap, libc, UEFI service, IRQ, absolute relocations or disk writes.
 */
typedef int (*intellect_fat32_sector_reader_t)(void *ctx, uint64_t lba, uint8_t out[512]);
typedef struct {
    intellect_fat32_sector_reader_t read;
    void *ctx;
    uint64_t disk_sectors, esp_lba, esp_sectors, data_lba;
    uint32_t fat_size, root_cluster, total_clusters;
    uint32_t reserved_sectors;
    uint8_t sec_per_cluster;
    int ready;
} intellect_fat32_mount_t;
typedef struct {
    uint32_t first_cluster, file_size;
    uint8_t is_dir;
} intellect_fat32_file_t;

/* Validate GPT primary header/entry CRC and select exactly one ESP. */
int intellect_fat32_mount_gpt(intellect_fat32_mount_t *m,
          intellect_fat32_sector_reader_t reader, void *ctx, uint64_t disk_sectors);
/* Only 8.3 ASCII paths, no symlinks, no write, LFN explicitly unsupported. */
int intellect_fat32_open(const intellect_fat32_mount_t *m,
          const char *path, intellect_fat32_file_t *out);
/* Bounded random-access read, number of returned bytes in *done. */
int intellect_fat32_read(const intellect_fat32_mount_t *m,
          const intellect_fat32_file_t *f, uint32_t offset,
          uint8_t *out, size_t length, size_t *done);
#endif
