# Memory Core M1 D2 — Verification Report

**Historical verification checkpoint:** 2026-11-19. The later repository-wide
baseline is reported in [MILESTONES.md](MILESTONES.md); this section does not
retroactively certify features added afterward.

BootInfo ABI: v1 (unchanged)

## Automated verification
- make clean check: 31 tests passed.
- Flat x86-64 ELF: no dynamic relocations.
- BIOS and UEFI disk image validation: passed.

## QEMU/OVMF
- Independent x86-64 kernel boot: passed.
- GDT/IDT/TSS initialization: passed.
- Physical memory manager: passed.
- PMM allocation/free self-test: passed.
- VMM high-half RW/NX self-test: passed.

## Physical Lenovo IdeaPad
- CPU: AMD A6-9225.
- Firmware: Phoenix Technologies Ltd.
- UEFI ExitBootServices: successful.
- BootInfo v1: validated.
- GDT/IDT/TSS: ready.
- PMM allocation/free: passed.
- VMM high-half RW/NX: passed.
- Final state: native kernel halted normally.

## Limitations
- Verified on one physical machine and QEMU.
- Full memory isolation and arbitrary hardware support are not established.
- Kernel heap, processes, scheduling and runtime code generation remain future work.
