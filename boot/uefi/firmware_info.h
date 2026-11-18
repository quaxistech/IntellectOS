#ifndef INTELLECT_UEFI_FIRMWARE_INFO_H
#define INTELLECT_UEFI_FIRMWARE_INFO_H

#include "efi_min.h"

/* Stage B is read-only: the caller must still be inside UEFI Boot Services. */
void uefi_output_ascii(efi_simple_text_output_t *out, const char *text);
void uefi_output_u32(efi_simple_text_output_t *out, uint32_t value);
void uefi_output_hex32(efi_simple_text_output_t *out, uint32_t value);
void uefi_print_firmware_info(const efi_system_table_t *system_table);

#endif
