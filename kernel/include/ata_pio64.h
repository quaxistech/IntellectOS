#ifndef INTELLECT_ATA_PIO64_H
#define INTELLECT_ATA_PIO64_H
#include <stdint.h>

/* S0: legacy primary-master ATA PIO, 28-bit LBA, 512-byte sectors.
 * No IRQ, DMA, writes, cache flush, IDENTIFY PACKET, hotplug or reset.
 * Real-hardware probing is restricted by block64 to a known PIIX IDE function.
 */
typedef struct {
    void *context;
    uint8_t (*in8)(void *,uint16_t);
    uint16_t (*in16)(void *,uint16_t);
    void (*out8)(void *,uint16_t,uint8_t);
} intellect_ata64_ops_t;
typedef struct {
    uint32_t sectors;
    char model[41];
} intellect_ata64_info_t;

enum { INTELLECT_ATA64_POLL_LIMIT=1000000u };
int intellect_ata64_identify(const intellect_ata64_ops_t *ops,intellect_ata64_info_t *out);
int intellect_ata64_read512(const intellect_ata64_ops_t *ops,uint32_t lba,
                             uint32_t capacity, uint8_t out[512]);
#endif
