# Intellect OS — Memory Core foundation (experimental)

## Scope and evidence

This patch implements a conservative **low-4-GiB physical-page allocator** and a
**minimal x86-64 exception diagnostic foundation** atop Stage E. It does not
implement virtual-memory management, W^X, guard pages, automatic page fault
recovery, heap allocation, SMP, user processes, or drivers. The EFI loader and
BootInfo v1 binary layout remain unchanged. The Legacy BIOS 0.4 sources are
not modified.

The memory allocator host fixture has passed on Linux. A freestanding
ELF64 link with **zero final relocations** has been checked in isolation.
Until the integrated project has passed `make clean check`, QEMU/OVMF and
Lenovo hardware tests, this patch **must not be presented as hardware-verified**.

## Bootstrap and ownership assumptions

* The Stage E bridge supplies BootInfo in RDI, CR3 in RSI, allocated stack top
  in RDX, and allocated kernel base in RCX. Those register values already
  existed in the original jump bridge; no BootInfo ABI expansion is needed.
* Stage E reserves 128 contiguous 4-KiB pages for the native kernel (including
  `.bss`), 16 pages for stack, 8 for page tables, and 64 for BootInfo staging.
  The linker independently refuses an in-memory image larger than 512 KiB.
* The normalized UEFI memory map exposes only `EfiConventionalMemory` (7)
  as `INTELLECT_MEMORY_USABLE`. BootServices, LoaderData, runtime, ACPI and
  MMIO regions remain unavailable for allocation.
* The PMM works only below 0x100000000 because existing identity tables cover
  the low 4 GiB. Physical pages above this boundary are never returned.
* Both 128-KiB bitmaps live inside the loader-reserved native kernel image.
  The `managed` bitmap tracks pages eligible for allocation; `busy` tracks
  allocated pages, preventing invalid and double frees.
* Any page partially overlapping an excluded region is excluded. Reserved
  UEFI descriptors override conventional-memory descriptors regardless of
  ordering. The first 1 MiB, EFI framebuffer, BootInfo/map, ACPI/SMBIOS
  entry-page hints and explicit kernel/stack/table/staging allocations are
  independently excluded.
* PMM allocation returns a physical address but does **not** clear memory.
  Consumers must zero or initialize pages before exposing them to any
  trust boundary. Page tables specifically must be zeroed before activation.
* The current interface is single-core/non-reentrant and has no allocator
  locks. Do not enable interrupts, SMP or preemption against it.

## Exception foundation

IDT entries 0–31 are provided. Error-code vectors are 8, 10–14, 17, 21,
29 and 30. GDT selectors 0x08/0x10 and a 64-bit TSS are established; the
#DF, #PF and NMI gates use dedicated IST stacks. All faults are fatal and
attempt to print vector, error code, RIP and (for #PF) CR2 on the framebuffer.
Nested faults halt without attempting repeated display. **No IRQ handlers,
PIC/APIC setup, recoverable fault handling, or privilege isolation exists.**

## Validation before approving or merging

```bash
cd /home/quaxis/IntellectOS
make clean check
make uefi-e-run
make run
```

Check the UEFI screen for `IDT / TSS / IST: INSTALLED`, nonzero managed/free
page counts and `PMM ALLOC/FREE: PASS`, with the former Stage E banner intact.
After QEMU validation, test the **UEFI** image on disposable USB using the
existing hardware procedure and verify the Linux system disk is not targeted.
Keep BIOS regressions separate. Deliberate `ud2` and page-fault injection tests
should run **in QEMU only**, after saving a stable boot image; do not inject
such faults on the physical Lenovo as a first validation step.

## Remaining work

1. Validate IDT error-frame layout and IST transitions through intentional
   QEMU fault injection, with independent exception-code and CR2 checks.
2. Add a paging API with mappings under 4-KiB granularity, NX/W^X and TLB
   invalidation, plus physical-frame initialization rules.
3. Add guard pages for stack, bounded kernel heap, and tests for corrupt or
   adversarial firmware maps, MMIO overlap, DMA restrictions and rollback.
4. Extend to RAM above 4 GiB only after full bootstrap mapping rules are
   designed and tested. Do not equate 'detected RAM' with 'allocatable RAM'.
