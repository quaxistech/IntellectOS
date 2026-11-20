#ifndef INTELLECT_HARDWARE64_H
#define INTELLECT_HARDWARE64_H
#include <stddef.h>
#include <stdint.h>
#include "bootinfo.h"

/* H1: bounded, read-only boot inventory. No heap, firmware calls or DMA. */
#define INTELLECT_HW64_MAX_PCI 128u
#define INTELLECT_HW64_MAX_ACPI 24u

typedef uint32_t (*intellect_hw64_pci_read_t)(void *ctx, uint8_t bus,
                            uint8_t device, uint8_t function, uint8_t offset);
typedef struct {
    uint8_t bus, device, function;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if, revision, header_type;
    uint8_t secondary_bus; /* meaningful for class 06/subclass 04 only */
} intellect_hw64_pci_t;

typedef struct {
    uint32_t cpu_max_leaf, cpu_signature, cpu_features_ecx, cpu_features_edx;
    char cpu_vendor[13];
    uint32_t memory_ranges;
    uint64_t conventional_mib; /* usable UEFI RAM only, not total installed RAM */
    uint64_t framebuffer_addr, framebuffer_size;
    uint8_t serial_ready, framebuffer_ready;
    uint8_t acpi_rsdp_valid, acpi_root_valid, acpi_revision;
    uint8_t smbios_reported, pci_cfg1_ready, pci_truncated, acpi_truncated;
    uint16_t pci_count, acpi_count;
    char acpi_table_signatures[INTELLECT_HW64_MAX_ACPI][5];
    intellect_hw64_pci_t pci[INTELLECT_HW64_MAX_PCI];
} intellect_hw64_snapshot_t;

/* Pure bounded parsers and discovery algorithm, including host-test fixtures. */
int intellect_hw64_rsdp_parse(const uint8_t *bytes, size_t len,
                         uint64_t *root_physical, int *uses_xsdt);
int intellect_hw64_sdt_validate(const uint8_t *bytes, size_t len,
                           uint32_t *table_size);
int intellect_hw64_range_allowed(const intellect_memory_range_t *ranges,
                    size_t count, uint64_t address, size_t length,
                    uint64_t rsdp_address);
size_t intellect_hw64_scan_pci(intellect_hw64_pci_read_t read, void *ctx,
                     intellect_hw64_pci_t *out, size_t capacity,
                     int *truncated);
/* Real x86-64 probe, after M3-A. Nonfatal if a platform feature is absent. */
int intellect_hw64_init(const intellect_bootinfo_t *boot);
const intellect_hw64_snapshot_t *intellect_hw64_snapshot(void);
#endif
