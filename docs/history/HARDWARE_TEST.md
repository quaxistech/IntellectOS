# Hardware and QEMU verification

## Confirmed checkpoints (user-observed screens)

| Target | Evidence | Scope |
|---|---|---|
| QEMU BIOS | BIOS Foundation 0.4 console | BootInfo, i386 kernel, VGA/COM1, CPU/E820/PCI |
| Lenovo Legacy BIOS | BIOS Foundation 0.4 console | AMD A6-9225, 15 E820 ranges, ~3182 MiB usable, 26 PCI functions, ACPI and SMBIOS found |
| QEMU/OVMF UEFI | Stage E native framebuffer screen | ExitBootServices and native x86-64 kernel; BootInfo v1 validated |
| Lenovo UEFI | Stage E native framebuffer screen | ExitBootServices reported success, 1920×1080 framebuffer, native x86-64 kernel, BootInfo v1 validated |

**Evidence level:** screenshots/console observations provided during development. Not automated machine-verifiable logs, continuous hardware tests, or proof that all firmware/device combinations work.

## Developer test procedure (Ubuntu 24.04)

```bash
make clean check
make run          # BIOS / QEMU
make uefi-e-run   # UEFI / OVMF
```

In BIOS, verify the `BootInfo ABI : v1 / validated` banner, CPU/E820/PCI output and clean halt. In UEFI, verify the screen is drawn **by the native kernel** and contains `EXITBOOTSERVICES: SUCCESS`, `BOOTINFO ABI: V1 VALIDATED`, and `NATIVE KERNEL RUNNING`.

`make check` verifies binary structure and executes host fixtures; it is **not** a hardware boot test. A screen displayed by an EFI application alone does not prove `ExitBootServices()` or independent kernel execution.

## USB / physical machine

- Back up anything important. This is an experimental OS; firmware or hardware behavior can differ.
- Verify USB identity with `lsblk -o NAME,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS` **immediately** before any manual raw write. Device names are not stable.
- Unmount every partition of the selected USB drive before writing an image. Writing an image destroys its existing filesystem contents; image builders themselves perform no physical writes.
- Read back image bytes with `cmp` **before mounting the FAT filesystem**, which can modify metadata.
- For BIOS, choose firmware Legacy support with the BIOS raw image. For UEFI, choose UEFI boot with the UEFI image; the EFI file is unsigned and requires appropriate Secure Boot policy.
- No HDD formatting or installation is necessary. The code does not implement disk writes. No USB keyboard, runtime power control, driver stack, or userland is present.
- If a test hangs or resets, record the last visible line and avoid changing multiple variables at once.
