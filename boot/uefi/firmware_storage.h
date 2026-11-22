#ifndef INTELLECT_FIRMWARE_STORAGE_H
#define INTELLECT_FIRMWARE_STORAGE_H
#include "efi_min.h"
/* Read-only capability discovery while EFI Boot Services are active.
 * This does NOT install native block drivers or survive ExitBootServices.
 */
typedef struct {
    unsigned total;
    unsigned removable; /* USB flash drives / removable media */
    unsigned fixed;     /* Fixed internal drives (HDD/SSD/NVMe) */
} intellect_uefi_storage_info_t;

unsigned intellect_uefi_block_device_count(efi_system_table_t *system_table);
unsigned intellect_uefi_block_device_inventory(efi_system_table_t *system_table,
                                               intellect_uefi_storage_info_t *info);
int intellect_uefi_is_removable_boot(efi_handle_t image_handle, efi_system_table_t *system_table);
#endif
