# IntellectOS L1 — USB boot welcome screen (2026-11-20)

**Build scope**: This stage integrates with existing M1/M2/C1/M3-A/H1/H2/S0 (latest locally tested IntellectOS). It is not a complete interactive Live OS, and is not an installer.

## Product goal

Boot on Lenovo UEFI from a USB memory device, enter the native freestanding x86-64 kernel, enumerate supported CPU/ACPI/PCI devices using existing H1, initialize existing H2 read-only bootstrap adapters, draw a welcome and diagnostics screen directly into the GOP-provided framebuffer after `ExitBootServices`, and keep the early COM1 monitor when available. The built-in ATA probe may be unavailable on a Lenovo lacking PIIX; it must not prevent the welcome screen. The boot image contains no target HDD operations or installers.

## Exact behavior

- Load the original UEFI EFI image `EFI/BOOT/BOOTX64.EFI` from the existing MBR/FAT16 hybrid USB image. No firmware, partition or BootInfo contract change.
- BootInfo v1 remains 152 bytes; all offsets remain unchanged; `uefi-check` and BIOS foundation tests continue.
- After M3-A and the H1/H2/S0 probes, renderer writes to the existing writable GOP framebuffer, using BGRX or RGBX 8:8:8:8 based on BootInfo. Software text uses the existing 5×7 font.
- Screen shows IntellectOS name, native system boot complete, CPU vendor, bounded PCI function count, ACPI table count, and read-only storage availability. It never claims the disk is ready if it isn't.
- Boot splash is purely informational: no keyboard, mouse, desktop GUI, window manager, filesystem root, installer or graphical interactions. COM1 monitor remains a separate, serial-only diagnostic interface if UART exists.
- Screen displays `LIVE PREVIEW - NO DISK WRITES - INSTALLER NOT YET AVAILABLE` to avoid misrepresenting scope.

## Build and test

```
python3 tools/apply_l1.py --project /home/quaxis/IntellectOS --check
python3 tools/apply_l1.py --project /home/quaxis/IntellectOS --apply
cd /home/quaxis/IntellectOS
git diff --check
make clean check
make uefi-s0-smoke
make uefi-live-smoke
make uefi-live-image
make uefi-console-gui
```

In the GUI QEMU window, visually inspect the screen. QEMU `-display none` smoke tests only serial output, **not the framebuffer**. `make uefi-live-image` creates `build/intellectos-live-usb.img` locally, not a physical disk write. The host framebuffer unit test is included in `make check` via `tests/test_welcome64.py`.

## USB flashing

```
lsblk -p -o NAME,SIZE,MODEL,TRAN,RM,TYPE,MOUNTPOINTS
bash tools/flash_live_usb.sh /dev/sdX
```

**Only the operator chooses `/dev/sdX`** after inspecting `lsblk`. The script requires transport `usb`, a whole unmounted disk, explicit typed confirmation `FLASH /dev/sdX`, and byte-for-byte verification. All existing data on that **USB** is destroyed. It refuses SSD/NVMe/ATA/HDD targets, partitions and mounted USB partitions. No explicit backup is required by the script; deciding to keep data on the USB is the user's responsibility.

On Lenovo: boot via UEFI boot picker (vendor-specific key), select USB. An unsigned third-party EFI image might require changing Secure Boot policy; do not change permanent firmware settings casually. UEFI firmware must provide GOP direct framebuffer and suitable memory mapping. If boot stops, capture a photo of the screen. On a machine without 16550 COM1, serial CLI will not be available; welcome remains visible.

## Out of scope and next work

No fully portable "install all drivers" API exists at OS level. Next required steps: robust PCIe ECAM/MCFG and BAR resource mapping, portable interrupt/IRQ/DMA, xHCI USB HID and mass storage, AHCI/NVMe, in-memory VFS and mount, then an interactive installer. Open source implementations are identified in `third_party/DRIVER_SOURCE_REGISTRY.md`; they are *not* silently compiled as native kernel drivers.
