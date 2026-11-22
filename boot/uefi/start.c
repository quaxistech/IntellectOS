/* Intellect OS UEFI Bootloader:
 * Dual-mode:
 * 1. Dedicated Installed OS Loader (when INTELLECT_UEFI_INSTALLED is defined):
 *    Directly starts the installed IntellectOS terminal session on the target drive.
 *    Zero installer menus, zero prompts, zero removable checks!
 * 2. Live USB / Installer Loader (default):
 *    Shows the high-definition boot menu (Option 1: Live Terminal, Option 2: Install to Disk).
 *    Timeout falls through to native kernel handoff for unattended testing / QEMU.
 */
#include "efi_min.h"
#include "firmware_info.h"
#include "platform_probe.h"
#include "bootinfo_builder.h"
#include "framebuffer_console.h"
#include "handoff.h"
#include "uefi_terminal.h"

#ifndef INTELLECT_UEFI_INSTALLED
#include "bootmenu.h"
#include "firmware_storage.h"
#endif

static const efi_char16_t banner[] = {
    'I','N','T','E','L','L','E','C','T',' ','O','S',' ','-',' ','U','E','F','I',' ','S','T','A','G','E',' ','E','\r','\n',0
};
static const efi_char16_t *volatile relocation_anchor = banner;

typedef struct {
    efi_table_header_t hdr;
    void *get_time, *set_time, *get_wakeup_time, *set_wakeup_time;
    void *set_virtual_address_map, *convert_pointer, *get_variable;
    void *get_next_variable_name, *set_variable, *get_next_high_monotonic_count;
    void (EFIAPI *reset_system)(uint32_t, efi_status_t, size_t, const efi_char16_t *);
} start_rt_services_t;

efi_status_t EFIAPI efi_main(efi_handle_t image_handle, efi_system_table_t *st) {
    if (st == NULL || st->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        st->hdr.header_size < sizeof(efi_system_table_t) || st->con_out == NULL ||
        st->con_out->output_string == NULL || st->boot_services == NULL)
        return EFI_INVALID_PARAMETER;

    (void)relocation_anchor;
    efi_simple_text_output_t *out = st->con_out;
    intellect_uefi_bootinfo_snapshot_t snapshot;
    if (!intellect_uefi_build_bootinfo(st, &snapshot)) {
        uefi_output_ascii(out, "INTELLECT OS: firmware/memory initialization failed.\r\n");
        return EFI_INVALID_PARAMETER;
    }

#ifdef INTELLECT_UEFI_INSTALLED
    /* =========================================================================
     * DEDICATED INSTALLED OS BOOTLOADER (Fixed HDD/SSD/NVMe).
     * There is NO installer menu here. It boots straight into IntellectOS!
     * ========================================================================= */
    intellect_uefi_handoff(image_handle, st, &snapshot);
#else
    /* =========================================================================
     * LIVE USB / INSTALLATION BOOTLOADER.
     * Shows boot menu:
     *   1: Live Terminal Mode
     *   2: Install to Disk
     * If user selects 1: runs interactive Live Terminal with USB keyboard.
     * If user selects 2: runs autonomous full target disk installer.
     * If timeout expires: falls through to native kernel handoff (for automated QEMU smoke tests).
     * ========================================================================= */
    unsigned blocks = intellect_uefi_block_device_count(st);
    intellect_efi_input_t *con_in = (intellect_efi_input_t *)st->con_in;

    int choice = intellect_uefi_bootmenu(snapshot.header, con_in, st->boot_services, blocks);
    if (choice == 1) {
        /* User explicitly selected 1: Live Terminal */
        intellect_uefi_terminal_run(image_handle, st, &snapshot, 0 /* is_live */);
        intellect_uefi_handoff(image_handle, st, &snapshot);
    } else if (choice == 2) {
        /* Installation complete: reboot to boot from newly installed disk */
        start_rt_services_t *rt = (start_rt_services_t *)st->runtime_services;
        if (rt && rt->reset_system) {
            rt->reset_system(0u /* EfiResetCold */, 0u, 0u, NULL);
        }
        for (;;) __asm__ volatile("pause");
    } else {
        /* Automated timeout or error: jump directly to native kernel for QEMU smoke tests */
        intellect_uefi_handoff(image_handle, st, &snapshot);
    }
#endif
}
