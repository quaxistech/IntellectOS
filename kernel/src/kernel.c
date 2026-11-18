#include <stdint.h>
#include "bootinfo.h"
#include "console.h"
#include "cpu.h"
#include "idt.h"
#include "pci.h"

static int bootinfo_valid(const intellect_bootinfo_t *b) {
    if (b == 0 || b->magic != INTELLECT_BOOTINFO_MAGIC ||
        b->version != INTELLECT_BOOTINFO_VERSION ||
        b->header_size != sizeof(*b) || b->total_size != sizeof(*b) ||
        (b->boot_method != INTELLECT_BOOT_BIOS && b->boot_method != INTELLECT_BOOT_UEFI) ||
        b->memory_map_stride != sizeof(intellect_memory_range_t) ||
        b->memory_map_count > 4096u ||
        (b->boot_method == INTELLECT_BOOT_BIOS &&
         b->memory_map_count > INTELLECT_MEMORY_MAP_CAPACITY) ||
        (b->flags & INTELLECT_FLAG_MEMORY_MAP_VALID) == 0u ||
        b->memory_map_count == 0u || b->memory_map_addr == 0u ||
        b->memory_map_addr > 0xFFFFFFFFu ||
        b->memory_map_count > (0x100000000ULL - b->memory_map_addr) / b->memory_map_stride) {
        return 0;
    }
    if (b->boot_method == INTELLECT_BOOT_BIOS && b->video_kind != INTELLECT_VIDEO_VGA_TEXT) return 0;
    return 1;
}

static void report_bootinfo(const intellect_bootinfo_t *boot) {
    const intellect_memory_range_t *map =
        (const intellect_memory_range_t *)(uintptr_t)(uint32_t)boot->memory_map_addr;
    uint64_t usable = 0u;
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < boot->memory_map_count; ++i) {
        if (map[i].type == INTELLECT_MEMORY_USABLE && map[i].length != 0u) {
            /* Avoid wrap if untrusted firmware claims impossibly large ranges. */
            if (UINT64_MAX - usable < map[i].length) usable = UINT64_MAX;
            else usable += map[i].length;
            ++count;
        }
    }
    console_write("BootInfo ABI : v1 / validated\n");
    console_write("Boot method  : ");
    console_write(boot->boot_method == INTELLECT_BOOT_BIOS ? "Legacy BIOS" : "UEFI");
    console_newline();
    if (boot->boot_method == INTELLECT_BOOT_BIOS) {
        console_write("BIOS drive/mode: ");
        console_hex32(boot->boot_drive);
        console_write(" / ");
        console_hex32(boot->bios_video_mode);
        console_newline();
    } else {
        console_write("UEFI spec rev: ");
        console_hex32(boot->uefi_revision);
        console_newline();
    }
    console_write("Memory map   : ");
    console_dec32(boot->memory_map_count);
    console_write(" ranges");
    if ((boot->flags & INTELLECT_FLAG_MEMORY_MAP_TRUNC) != 0u) console_write(" (TRUNCATED)");
    console_newline();
    console_write("Usable RAM   : approx ");
    console_dec32((uint32_t)(usable >> 20));
    console_write(" MiB / ");
    console_dec32(count);
    console_write(" ranges\n");
    console_write("Firmware tbls: ACPI=");
    console_write(boot->acpi_rsdp_addr != 0u ? "found" : "none");
    console_write(" SMBIOS=");
    console_write(boot->smbios_addr != 0u ? "found" : "none");
    console_newline();
}

void kernel_main(const intellect_bootinfo_t *boot) {
    idt_init();
    console_init();
    console_write("=======================================\n");
    console_write(" INTELLECT OS - BIOS FOUNDATION 0.4\n");
    console_write(" NATIVE i386 / NO GRUB / NO LINUX\n");
    console_write("=======================================\n");
    if (!bootinfo_valid(boot)) {
        console_write("FATAL: BootInfo ABI missing or invalid.\n");
        return;
    }
    report_bootinfo(boot);
    cpu_report();
    pci_report();
    console_write("Kernel status: IDLE (no keyboard or power controls)\n");
    console_write("USB/HDD writes: DISABLED. Device drivers: NONE.\n");
    /* Interrupts remain masked. The entry assembly performs cli/hlt on return. */
}
