# Architecture — BIOS and UEFI native boot foundations

## Scope

Intellect OS explores hardware-aware generation of native OS components. The current implementation is a **deterministic bare-metal foundation**; it does not run AI. Firmware selects Legacy BIOS or UEFI **before** Intellect OS code starts. The two entry paths share `Intellect BootInfo v1` but do not yet share one machine-code kernel.

## Legacy BIOS (physically verified)

```text
BIOS → MBR (0x7c00) → Stage1 → BootInfo v1 → i386 protected-mode kernel
                                            → VGA/COM1, CPU, E820, PCI → halt
```

Stage0 uses BIOS INT13 extensions to read Stage1 from raw sectors. Stage1 obtains E820 and checks optional ACPI/SMBIOS entry points, loads the fixed-size i386 kernel, enables A20, configures GDT and enters 32-bit protected mode. After that, the kernel does not make BIOS interrupt calls. It uses VGA text memory, COM1 and basic PCI configuration reads. Stage1 requires a compatible Legacy BIOS; unsupported machines must use another entry path.

### BIOS bootstrap physical layout

| Address | Use |
|---|---|
| `0x0500` | BIOS boot-drive byte |
| `0x5000..0x5097` | 152-byte BootInfo header |
| `0x6000..0x65FF` | Up to 64 × 24-byte normalized memory ranges |
| `0x7C00..0x7DFF` | MBR |
| `0x8000..0x9FFF` | Stage1 |
| `0x10000..0x1FFFF` | i386 kernel load window |
| `0x70000` downward | Early i386 stack |
| `0xB8000` | VGA text memory |

The BIOS kernel is paged **off** and executes with no user/kernel isolation. Its E820 map is not proven free of overlapping entries and cannot be treated as a physical RAM capacity report.

## UEFI x86-64 (physically verified)

```text
UEFI → BOOTX64.EFI → GOP / memory map / ACPI / SMBIOS
                  → BootInfo v1 + allocated native kernel/stack/page tables
                  → final GetMemoryMap / MapKey → ExitBootServices
                  → assembly ABI bridge → native ELF-derived x86-64 kernel
                  → direct framebuffer diagnostics → halt
```

The EFI loader is a PE32+ application; its native kernel is a **separate flat position-independent image embedded in the EFI file**. Allocations for the kernel, stack, page tables and BootInfo are made before the final map fetch. UEFI `ExitBootServices()` is called with the exact current `MapKey` and bounded retries on `EFI_INVALID_PARAMETER`; successful transition is irreversible. Native kernel code uses no UEFI Boot Services or console services.

The x86-64 page tables provide bootstrap **identity mappings** using 2-MiB pages, with mapped framebuffer areas marked conservatively uncached. They are not a full virtual memory system; no W^X, process isolation, general allocator or safe reclamation is implemented.

The preliminary UEFI BootInfo snapshot from Stage D is not the final handoff map. Stage E re-reads and normalizes the memory map before exiting firmware. Firmware memory classifications and loader-owned allocations remain conservatively reserved.

## Shared BootInfo contract

The canonical declaration is `kernel/include/bootinfo.h`, with fixed layout documented in [BOOTINFO_V1.md](BOOTINFO_V1.md). The header is 152 bytes; memory ranges are 24 bytes each. Physical addresses remain integer address fields and must be interpreted against the active bootstrap mapping.

The BIOS kernel is **i386** and the UEFI kernel **x86-64**. Sharing BootInfo does not by itself create a universal binary. Future common-kernel work must preserve the ABI while explicitly resolving CPU mode, entry convention, virtual addressing and ownership semantics.

## Image format and safety

Both outputs are 32-MiB raw USB-HDD images; the active BIOS partition starts at LBA 2048, Stage1 is stored at LBAs 1–16 and BIOS kernel at LBAs 17–144. The UEFI image adds `EFI/BOOT/BOOTX64.EFI` to the FAT16 partition and sets the partition type to EFI System. The builder writes **only** generated files under `build/`. There is no general FAT driver in either kernel, and neither has an installer or disk-write API.
