#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fat32_vfs64.h"

static FILE *disk_file = NULL;

static int disk_read_sector(void *ctx, uint64_t lba, uint8_t out[512]) {
    (void)ctx;
    if (!disk_file) return 0;
    if (fseeko(disk_file, (off_t)lba * 512, SEEK_SET) != 0) return 0;
    return fread(out, 1, 512, disk_file) == 512;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <disk_image> <efi_file>\n", argv[0]);
        return 2;
    }

    disk_file = fopen(argv[1], "rb");
    if (!disk_file) {
        perror("fopen disk");
        return 3;
    }
    if (fseeko(disk_file, 0, SEEK_END) != 0) return 4;
    off_t disk_sz = ftello(disk_file);
    if (disk_sz < 0 || disk_sz % 512 != 0) return 5;
    uint64_t total_sectors = (uint64_t)disk_sz / 512;

    FILE *efi_f = fopen(argv[2], "rb");
    if (!efi_f) {
        perror("fopen efi");
        return 6;
    }
    if (fseeko(efi_f, 0, SEEK_END) != 0) return 7;
    off_t efi_sz = ftello(efi_f);
    if (efi_sz < 0) return 8;
    if (fseeko(efi_f, 0, SEEK_SET) != 0) return 9;

    uint8_t *expected = (uint8_t *)malloc((size_t)efi_sz);
    if (!expected) return 10;
    if (fread(expected, 1, (size_t)efi_sz, efi_f) != (size_t)efi_sz) {
        free(expected);
        return 11;
    }
    fclose(efi_f);

    intellect_fat32_mount_t m;
    if (!intellect_fat32_mount_gpt(&m, disk_read_sector, NULL, total_sectors)) {
        fprintf(stderr, "intellect_fat32_mount_gpt failed\n");
        free(expected);
        return 12;
    }

    intellect_fat32_file_t f;
    if (!intellect_fat32_open(&m, "/EFI/BOOT/BOOTX64.EFI", &f)) {
        if (!intellect_fat32_open(&m, "EFI/BOOT/BOOTX64.EFI", &f)) {
            fprintf(stderr, "intellect_fat32_open failed\n");
            free(expected);
            return 13;
        }
    }

    if (f.file_size != (uint32_t)efi_sz) {
        fprintf(stderr, "Size mismatch: file_size=%u, expected=%zu\n", f.file_size, (size_t)efi_sz);
        free(expected);
        return 14;
    }

    uint8_t *actual = (uint8_t *)malloc((size_t)efi_sz);
    if (!actual) {
        free(expected);
        return 15;
    }

    size_t done = 0;
    if (!intellect_fat32_read(&m, &f, 0, actual, (size_t)efi_sz, &done) || done != (size_t)efi_sz) {
        fprintf(stderr, "intellect_fat32_read failed, done=%zu\n", done);
        free(expected);
        free(actual);
        return 16;
    }

    if (memcmp(expected, actual, (size_t)efi_sz) != 0) {
        fprintf(stderr, "Content mismatch\n");
        free(expected);
        free(actual);
        return 17;
    }

    free(expected);
    free(actual);
    fclose(disk_file);

    puts("PASS: native freestanding FAT32 VFS");
    return 0;
}
