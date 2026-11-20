#ifndef INTELLECT_DRIVER64_H
#define INTELLECT_DRIVER64_H
#include <stdint.h>
#include "hardware64.h"

/* H2: kernel-internal driver contracts. No user-provided driver blobs.
 * Probe is observational; activate may acquire only declared resources.
 * The 4 built-in descriptors are implemented, not dynamically linked.
 */
enum {
    INTELLECT_DRIVER64_UART=1,
    INTELLECT_DRIVER64_FRAMEBUFFER=2,
    INTELLECT_DRIVER64_PCI_CONFIG=3,
    INTELLECT_DRIVER64_ACPI_TABLES=4,
    INTELLECT_DRIVER64_ATA_PIO=5,
    INTELLECT_DRIVER64_COUNT=5
};
enum { INTELLECT_DRIVER64_UNAVAILABLE=0, INTELLECT_DRIVER64_ACTIVE=1, INTELLECT_DRIVER64_PROBING=2 };
enum { INTELLECT_DRIVER64_PIO=1, INTELLECT_DRIVER64_MMIO=2 };
typedef struct {
    uint8_t owner, domain;
    uint16_t reserved;
    uint64_t base, length;
} intellect_driver64_claim_t;
typedef struct {
    uint8_t id, state;
    uint16_t attached;
    uint32_t capabilities; /* bit 0: inspection; bit 1: char I/O; bit 2: display */
} intellect_driver64_record_t;

int intellect_driver64_init(const intellect_hw64_snapshot_t *hw);
const intellect_driver64_record_t *intellect_driver64_get(unsigned id);
/* Exclusive in-memory resource arbitration. Does NOT configure an MMU/IOMMU.
 * No resource is granted to arbitrary PCI functions at this stage. */
int intellect_driver64_claim(unsigned driver, unsigned domain,
                            uint64_t base, uint64_t length);
unsigned intellect_driver64_claim_count(void);
/* Atomic paired PIO resource reservation for identified ATA controller. */
int intellect_driver64_begin_pio_ata(void);
int intellect_driver64_finish_pio_ata(int success);
const char *intellect_driver64_name(unsigned id);
const char *intellect_driver64_pci_class(const intellect_hw64_pci_t *p);
/* A supported class does not imply a functioning controller driver. */
const char *intellect_driver64_pci_requirement(const intellect_hw64_pci_t *p);
#endif
