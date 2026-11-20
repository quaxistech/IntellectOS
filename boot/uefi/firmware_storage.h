#ifndef INTELLECT_FIRMWARE_STORAGE_H
#define INTELLECT_FIRMWARE_STORAGE_H
#include "efi_min.h"
/* Read-only capability discovery while EFI Boot Services are active.
 * This does NOT install native block drivers or survive ExitBootServices.
 */
unsigned intellect_uefi_block_device_count(efi_system_table_t *system_table);
#endif
