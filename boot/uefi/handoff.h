#ifndef INTELLECT_UEFI_HANDOFF_H
#define INTELLECT_UEFI_HANDOFF_H
#include "efi_min.h"
#include "bootinfo_builder.h"
/* No-return handoff after successful ExitBootServices. */
__attribute__((noreturn)) void intellect_uefi_handoff(efi_handle_t image,
    efi_system_table_t *st, intellect_uefi_bootinfo_snapshot_t *snapshot);
#endif
