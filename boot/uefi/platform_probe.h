#ifndef INTELLECT_UEFI_PLATFORM_PROBE_H
#define INTELLECT_UEFI_PLATFORM_PROBE_H
#include "efi_min.h"

/* Stage C is a read-only probe, NOT a kernel handoff. */
void uefi_probe_platform(const efi_system_table_t *system_table);
#endif
