# Verified milestones and known observations

This is the condensed history of **tests observed by the project owner**, not independent laboratory certification. Older Stage A–D instruction documents were consolidated here to remove outdated active build guidance.

| Stage | QEMU | Physical Lenovo | What was verified |
|---|---|---|---|
| Seed 0.1 | Yes | Limited | GRUB/Multiboot 32-bit kernel with VGA, CPU, E820, PCI; GRUB console mismatch seen on Lenovo |
| BIOS diagnostic 0.2 | Yes | Yes | Custom MBR + Stage1, BIOS EDD, CPU and E820 diagnostics without GRUB |
| BIOS kernel 0.3 | Yes | Yes | Direct VGA 32-bit protected-mode kernel; Lenovo PCI found 26 functions; keyboard/power experiments later removed |
| BIOS Foundation 0.4 | Yes | Yes | BootInfo v1, CPU/RAM/PCI, ACPI and SMBIOS discovery, no disk writes |
| UEFI A | Yes | Yes | Native `BOOTX64.EFI` launch and UEFI text console |
| UEFI B | Yes | Yes | UEFI version and firmware vendor/revision fields |
| UEFI C | Yes | Yes | GOP, memory map, ACPI and SMBIOS; fixed `LocateProtocol` ABI offset bug (`320`, not `312`) |
| UEFI D | Yes | Yes | Validated pre-ExitBootServices BootInfo v1 snapshot; **not** a native kernel |
| UEFI E | Yes | Yes | ExitBootServices; independent native x86-64 kernel uses framebuffer and validated BootInfo v1 |

### Physical Lenovo data from Stage E screenshot

- UEFI interface revision 2.50; firmware vendor Phoenix Technologies Ltd.
- GOP 1920×1080 and framebuffer at `0xE0000000`.
- BootInfo v1 validated; independent kernel reports 57 normalized memory ranges (the value may change with different firmware allocations).
- Screen reports `EXITBOOTSERVICES: SUCCESS`, `UEFI BOOT SERVICES: NOT USED`, and `NATIVE KERNEL RUNNING - CPU HALTED`.

### Host/source checks

- Uploaded source archive contained generated build outputs and Python bytecode, which are excluded from Git by `.gitignore` and removed from the cleaned source bundle.
- Clean `make check` on the source snapshot completed with **23 tests passing**.
- BIOS output was byte-for-byte the same as the archived BIOS image when rebuilt in the audit environment. The UEFI EFI/kernel bytes **differed across toolchain environments** while passing structural checks; the PE/COFF timestamp is pinned, but a pinned timestamp alone does not guarantee cross-toolchain reproducibility.

Build artifacts, firmware versions and host toolchain must be recorded to claim byte-for-byte release reproducibility.
