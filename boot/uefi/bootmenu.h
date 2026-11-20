#ifndef INTELLECT_BOOTMENU_H
#define INTELLECT_BOOTMENU_H
#include "efi_min.h"
#include "../../kernel/include/bootinfo.h"

/* UEFI Simple Text Input Protocol, valid ONLY before ExitBootServices.
 * UEFI 2.x: Reset (+0), ReadKeyStroke (+8), WaitForKey (+16).
 */
typedef struct { uint16_t scan_code, unicode_char; } intellect_efi_key_t;
typedef struct intellect_efi_input {
    efi_status_t (EFIAPI *reset)(struct intellect_efi_input *, uint8_t extended);
    efi_status_t (EFIAPI *read_key)(struct intellect_efi_input *, intellect_efi_key_t *);
    void *wait_for_key;
} intellect_efi_input_t;

/* Return 1 for Terminal Edition, 0 for unusable video/input. Graphical Edition
 * is intentionally not selectable; selecting it displays an explanation.
 */
int intellect_uefi_bootmenu(const intellect_bootinfo_t *boot,
                            intellect_efi_input_t *input,
                            efi_boot_services_t *services,
                            unsigned block_devices);
#endif
