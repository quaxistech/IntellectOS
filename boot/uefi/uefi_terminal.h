#ifndef INTELLECT_UEFI_TERMINAL_H
#define INTELLECT_UEFI_TERMINAL_H

#include "efi_min.h"
#include "bootinfo_builder.h"
#include "../../kernel/include/bootinfo.h"

#ifndef INTELLECT_EFI_INPUT_T_DEFINED
#define INTELLECT_EFI_INPUT_T_DEFINED
typedef struct { uint16_t scan_code, unicode_char; } intellect_efi_key_t;
typedef struct intellect_efi_input {
    efi_status_t (EFIAPI *reset)(struct intellect_efi_input *, uint8_t extended);
    efi_status_t (EFIAPI *read_key)(struct intellect_efi_input *, intellect_efi_key_t *);
    void *wait_for_key;
} intellect_efi_input_t;
#endif

/* Interactive High-Definition Terminal for IntellectOS.
 * Runs on UEFI GOP framebuffer with native firmware USB/PS2 keyboard input.
 * is_installed: 1 if booted from internal HDD/SSD, 0 if booted from Live USB.
 */
void intellect_uefi_terminal_run(efi_handle_t image_handle,
                                 efi_system_table_t *st,
                                 intellect_uefi_bootinfo_snapshot_t *snapshot,
                                 int is_installed);

#endif /* INTELLECT_UEFI_TERMINAL_H */
