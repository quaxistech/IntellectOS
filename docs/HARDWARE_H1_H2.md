# IntellectOS — H1 Hardware Discovery + H2 Bootstrap Driver Framework

**Date:** 2026-11-20. **Status:** candidate implementation; acceptance requires a successful build and a real QEMU/OVMF guest run.  **Base:** Memory Core M2 + C1 + M3-A (not the older `main` branch). **BootInfo:** unchanged v1, 152-byte header, 24-byte memory records.

## Purpose and scope

H1 produces a bounded inventory of real hardware visible at early kernel boot, independently of firmware services after `ExitBootServices`. H2 names and tracks the **four presently implemented built-in adapters**, without claiming that enumeration equals a fully functional device driver:

| Driver | Status | Capabilities | What it cannot do |
|---|---|---|---|
| `uart16550-polling` | Active if existing C1 COM1 probe succeeds | Character I/O | IRQ-driven TTY, terminal multiplexing, hot-plug |
| `gop-framebuffer-handoff` | Active if validated BootInfo framebuffer is present | Pixel output inherited from loader | Mode switching, GPU acceleration, display hot-plug |
| `pci-config-type1-readonly` | Active if x86 PCI mechanism #1 is responsive | PCI config reads and class enumeration, segment 0 | ECAM-only systems, PCI segments >0, IRQ/MSI, BAR mapping, DMA, config writes |
| `acpi-table-validator` | Active if valid RSDP/root SDT is found | ACPI table checksums/signatures | AML interpreter, full topology, ACPI power/thermal control |

This is **H2 bootstrap driver registry**, not the full production driver subsystem. No driver loading from disk, bind/rebind of arbitrary third-party modules, device DMA, IRQ routing or user-space driver isolation is implemented. Device-class detection for NVMe, AHCI, xHCI, VirtIO/network means *candidate device discovered, no controller driver*. The real storage/USB/network controllers belong to H2.1/H2.2 after VMM/IOMMU/IRQ prerequisites.

## Data model and requirements

All state resides in bounded BSS arrays, with no heap or allocations during PCI enumeration. The built-in driver layer maintains an exclusive in-memory PIO/MMIO claim registry; overlaps are rejected before a second owner is admitted. This is resource accounting, not hardware-enforced DMA or MMIO isolation. `INTELLECT_HW64_MAX_PCI=128`; after capacity is reached, a truncation marker is required. ACPI signature inventory stores at most 24 valid entries with its own truncation marker. A complete platform inventory is **not** promised: legacy config #1 is a fallback and does not cover ECAM-only PCIe; no ACPI AML root-bridge traversal is attempted. The `hw tree` command is a flattened PCI segment-0 inventory, not an accurate ACPI hierarchy.

- H1 collects CPUID vendor/max basic leaf; the existing normalized BootInfo memory-map count; validated ACPI RSDP and RSDT/XSDT table signatures; CPU signature and CPUID(1) feature words; usable UEFI conventional RAM in MiB (not total installed memory); firmware-reported SMBIOS presence (no SMBIOS string/table parsing); detected PCI segment-0 devices; existing UART and GOP status.
- PCI config scanning is **read-only** with legacy port `0xCF8/0xCFC`, performing bus/device/function probes and reading IDs, class codes, header type and secondary bus of detected bridges. Only the config-address latch is written, never device configuration registers. It runs while interrupts remain disabled, before SMP. Future systems require a lock around this latch.
- ACPI RSDP follows the ACPI 1.0/2+ checksum contract; SDT pointers must be below 4GiB, have bounded lengths, map entirely into an allowed firmware-described ACPI/reclaimed region, have matching signatures and pass table checksum verification. The RSDP may reside in the BootInfo-reported reserved region; other arbitrary reserved/MMIO regions are not accepted as ACPI tables. Failures do not halt the OS; they disable ACPI inventory. The code does not execute AML.
- All snapshot records are kernel-internal. They are **not** part of BootInfo, the IVX ABI, an on-disk format or a stable public ABI. No binary on-disk migration is needed.
- Built-in H2 state is identified by numeric driver IDs. No global static array of function pointers requiring runtime absolute relocation is generated. Future descriptors should be registered at runtime, enforce ownership of MMIO/PIO/IRQ/DMA resources, and support explicit `DISCOVERED -> PROBED -> BOUND -> STARTED -> STOPPED` state transitions.

## Command surface

In the existing early serial diagnostic monitor:

- `hw` / `hw help` — command synopsis and warnings.
- `hw summary` — one-line CPU, PCI, ACPI, RAM map and platform status.
- `hw list` — summary plus each detected PCI function.
- `hw cpu` — CPUID vendor and max basic leaf.
- `hw acpi` — validated ACPI/SMBIOS summary and table signatures.
- `hw pci` — discovered PCI functions, BDF, vendor/device IDs, class, and missing controller-driver marker.
- `hw tree` — flat PCI hierarchy, with bridge secondary-bus field (not a verified physical tree).
- `hw drivers` — four built-in adapters and state (`ACTIVE`, `UNAVAILABLE`).

No command installs software, edits device configuration, reads/writes a disk, reformats a partition, loads untrusted binaries or enables DMA.

## Fault handling and security

- All untrusted firmware lengths use explicit bounds and checksums. ACPI entry pointers beyond the mapped 4GiB physical identity window are **not** dereferenced.
- The memory-range check is a conservative best effort based on BootInfo. A malicious firmware memory map is still outside the safety model; stronger validated physical mapping is required before scanning unknown hardware on arbitrary production machines.
- Missing ACPI, PCI config mechanism, UART or GOP is not conflated with an absent CPU or failed kernel. The existing Stage E currently **does** require GOP/COM1 for usable diagnostics; C2 will remove the mandatory display requirement.
- No ACPI power methods, port reads outside PCI config, BAR decoding or bus-master writes are made. Future bus driver claims must be denied until DMA isolation, interrupts and memory ownership are implemented.
- Hardcoded PCI driver-name suggestions are descriptive strings, not ready drivers. Unknown hardware remains unbound. No silent fallback to a random device-specific driver.

## Acceptance gates

1. **Preflight:** `apply_h1_h2.py --check` accepts the real local M3-A tree and refuses missing exact anchors or pre-existing new files.
2. **Patch:** `--apply` changes only `Makefile`, `kernel/src64/kernel.c`, `kernel/src64/kmonitor64.c`, `tests/test_console64.py`, and creates 10 new H1/H2 files. Existing 13+ specifications under `docs/` remain untouched.
3. **Host suite:** `make clean check` passes all pre-existing M1/M2/C1/M3-A tests plus 2 H1/H2 Python tests. `verify_kernel64.py` passes, no unsupported GOT/dynamic relocations.
4. **Guest:** `make uefi-hw-smoke` reaches all three guest checkpoints: `M3-A IDENTITY W^X/NX: PASS`, `H1/H2 HARDWARE INVENTORY: READY`, `H1/H2 DRIVER REGISTRY: READY`; then runs `hw summary`, `hw drivers`, `hw pci`, `hw acpi`, `hw cpu` and `echo` over COM1.
5. **Negative cases:** host fixtures verify ACPI checksums and length limits, malformed SDTs, multi-function PCI and bridge detection, sparse bus discovery, snapshot capacity/truncation, driver-state invariants and dispatch nonrecognition.
6. **Hardware:** Physical Lenovo testing is separate from this QEMU gate. Do not infer real xHCI/NVMe support from the QEMU inventory and do not make physical disk writes. Installer/Live USB is a later stage.

## Ordered follow-up

H2.1 **PCI/ACPI resource discovery**: verified MCFG ECAM mapping, AML-independent root bridge coverage, resource ownership, safe MMIO allocator, interrupt domain, PCI capabilities, MSI/MSI-X, IOMMU awareness.

H2.2 **Device drivers**: USB xHCI + HID, NVMe and AHCI block I/O (read-only first), VirtIO blk/net for QEMU, framebuffer/keyboard TTY. Storage writes only after failure-injection tests and explicit target selection.

H3 **IntellectOS Live USB**: init RAM disk/rootfs, independent userspace shell/installer runtime, full hardware diagnostics, optional display. I1 installer comes only after block I/O, GPT validation, boot recovery and IntellectFS durability gates. Current QEMU image is not an installer.
