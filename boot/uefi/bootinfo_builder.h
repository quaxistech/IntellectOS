#ifndef INTELLECT_UEFI_BOOTINFO_BUILDER_H
#define INTELLECT_UEFI_BOOTINFO_BUILDER_H

#include "efi_min.h"
#include "../../kernel/include/bootinfo.h"

/* Stage D only: EFI LoaderData-backed normalized snapshot, NOT final EBS map.
 * Physical address comes from EFI AllocatePages rather than a guessed pointer.
 * Its ownership must be transferred formally before a future kernel handoff.
 */
typedef struct {
    intellect_bootinfo_t *header;
    const intellect_memory_range_t *ranges;
    uint64_t physical_base;
    uint32_t descriptor_count;
    uint32_t descriptor_stride;
    uint32_t descriptor_version;
    uint32_t normalized_count;
    uint32_t usable_count;
    uint64_t usable_mib;
    uint8_t address_below_4g;
} intellect_uefi_bootinfo_snapshot_t;

int intellect_uefi_build_bootinfo(const efi_system_table_t *system_table,
                                 intellect_uefi_bootinfo_snapshot_t *result);
int intellect_uefi_validate_bootinfo(const intellect_uefi_bootinfo_snapshot_t *snapshot);
void intellect_uefi_print_bootinfo(const efi_system_table_t *system_table,
                                   const intellect_uefi_bootinfo_snapshot_t *snapshot);
/* Stage E: update the SAME preallocated BootInfo after a fresh GetMemoryMap.
 * These helpers do not call firmware or allocate memory. The loader must call
 * ExitBootServices immediately afterward using that GetMemoryMap's key.
 */
void *intellect_uefi_final_map_buffer(const intellect_uefi_bootinfo_snapshot_t *snapshot);
size_t intellect_uefi_final_map_capacity(void);
int intellect_uefi_refresh_bootinfo_map(intellect_uefi_bootinfo_snapshot_t *snapshot,
                                        size_t bytes, size_t stride, uint32_t version);
#endif
