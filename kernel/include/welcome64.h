#ifndef INTELLECT_WELCOME64_H
#define INTELLECT_WELCOME64_H
#include <stdint.h>
#include "bootinfo.h"

/* Display-only Live USB welcome. No firmware calls, heap, interrupts, disk I/O.
 * All pointer inputs must be kernel-owned, bounded NUL-terminated text.
 * Called after H1/H2/S0 initialization, before starting the serial monitor.
 * This is a UI milestone, NOT a native GPU/USB/storage driver.
 */
typedef struct {
    const char *cpu_vendor;
    uint32_t pci_functions;
    uint32_t acpi_tables;
    uint8_t acpi_valid;
    uint8_t storage_readonly_ready;
    const char *storage_model;
    uint64_t storage_sectors;
} intellect_welcome64_info_t;

/* Returns 1 only if a bounded, framebuffer-backed render was performed. */
int intellect_welcome64_render(const intellect_bootinfo_t *boot,
                              const intellect_welcome64_info_t *info);
#endif
