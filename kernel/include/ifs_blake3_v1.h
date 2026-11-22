#ifndef INTELLECT_IFS_BLAKE3_V1_H
#define INTELLECT_IFS_BLAKE3_V1_H
#include <stddef.h>
#include <stdint.h>
/* Portable BLAKE3-256 unkeyed mode; no libc/heap/firmware access. */
void intellect_ifs_blake3(const uint8_t *data, size_t len, uint8_t out[32]);
#endif
