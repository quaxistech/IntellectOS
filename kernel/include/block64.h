#ifndef INTELLECT_BLOCK64_H
#define INTELLECT_BLOCK64_H
#include <stddef.h>
#include <stdint.h>
#include "hardware64.h"

enum { INTELLECT_BLOCK64_SECTOR_BYTES=512u };
typedef struct {
    uint64_t sectors;
    uint32_t sector_bytes;
    uint8_t ready, readonly;
    char model[41];
} intellect_block64_info_t;
/* S0 supports one probed, read-only PIIX primary master. No physical writes. */
/* Pure PCI capability gate; host-testable without touching a real device. */
int intellect_block64_piix_supported(const intellect_hw64_snapshot_t *hardware);
int intellect_block64_init(const intellect_hw64_snapshot_t *hardware);
const intellect_block64_info_t *intellect_block64_info(void);
int intellect_block64_read(uint64_t lba,uint8_t out[512]);
#ifdef INTELLECT_BLOCK64_HOST_TEST
/* Host fixture requires an explicit I/O backend; never touches port space. */
#include "ata_pio64.h"
int intellect_block64_host_init(const intellect_ata64_ops_t *ops);
#endif
#endif
