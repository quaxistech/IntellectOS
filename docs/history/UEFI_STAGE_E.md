# UEFI Stage E — independent native x86-64 kernel handoff

## Status

Stage E was observed to complete in **QEMU/OVMF and on a physical Lenovo IdeaPad**. Screens show the native kernel banner, validated BootInfo v1, `EXITBOOTSERVICES: SUCCESS`, and `NATIVE KERNEL RUNNING - CPU HALTED`. Build/test success and runtime observation are distinct levels of evidence; firmware portability and all memory mappings remain unproven outside the exercised cases.

## Goal and executable boundaries

Stage E joins the complete E1/E2/E3 vertical slice. The software stack has two binaries: a PE32+ EFI loader (`BOOTX64.EFI`) and a freestanding flat x86-64 kernel (`kernel64.bin`) embedded in the loader. The Legacy BIOS 0.4 loader/32-bit kernel source remains unchanged.

## E1 — framebuffer console

The loader discovers GOP, validates pixel format/geometry, and writes the pre-exit display using a small bitmap-font rasterizer without `ConOut.OutputString`. Unsupported `BltOnly`, unsupported pixel formats or invalid bounds stop the handoff. UEFI console services are not used after the final map acquisition.

## E2 — memory layout and firmware exit

1. Allocate 64 pages (256 KiB) below 4 GiB for BootInfo plus normalized/raw memory maps. Reserve kernel (32 pages), stack (16 pages) and bootstrap page tables (8 pages) separately.
2. Construct 4-level identity mappings using 2-MiB pages for low 4 GiB and bounded higher GOP framebuffer mappings; framebuffer-overlapping pages use uncached attributes. These are bootstrap mappings, **not** a full memory manager.
3. Fetch the final memory map into the preallocated raw buffer, validate descriptor stride, bounds, and types, and normalize into BootInfo v1 without new allocations.
4. Call `ExitBootServices()` with the exact `MapKey`. On `EFI_INVALID_PARAMETER`, retry only `GetMemoryMap()` and `ExitBootServices()` up to eight times. No UEFI console calls occur after the first exit attempt.
5. On success hand off to the native kernel through an assembly bridge (Microsoft x64 calling convention to SysV x86-64). Error stripes E01–E06 are direct framebuffer diagnostics and do not attempt firmware recovery.

## E3 — native kernel

The ELF-derived flat kernel has entry offset zero, no imports or dynamic relocations. The entry disables interrupts, loads bootstrap CR3, switches to the allocated native stack, validates BootInfo v1, draws directly into the framebuffer, and halts the boot CPU with `cli; hlt`.

No runtime UEFI Boot Services are used. ACPI and SMBIOS physical addresses are printed, not deep-copied or dereferenced by the kernel. Firmware-owned and loader-owned regions remain conservatively reserved.

## Tests and limits

- `make check`: BIOS image verifier, UEFI PE/FAT verifier, flat native kernel verifier and host test fixtures (23 passing on this source snapshot).
- `make uefi-e-run`: manual QEMU/OVMF run with OVMF variable-state copy; physical Lenovo experiment separately observed by the user.
- Host fixtures simulate a stale `MapKey` followed by a successful retry and validate the allocation/memory-map sequencing. This does not cover all real firmware behavior.
- The PE/COFF timestamp is fixed to zero; **full cross-toolchain reproducibility is not yet guaranteed** (see [release baseline](RELEASE_BASELINE.md)).
- No scheduler, interrupts, IOMMU, allocator, user mode, W^X or disk-write support. The kernel currently halts instead of returning to firmware.

Next task: exception/physical-memory management and independent tests before implementing the planned verified native-code generator or local model execution.
