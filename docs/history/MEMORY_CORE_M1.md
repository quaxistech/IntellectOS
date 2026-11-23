# Intellect OS — Memory Core M1 (experimental)

## Scope

This M1 increment **repairs the missing native kernel argument bridge** in the
previous Foundation package. The EFi/UEFI assembly handoff already leaves the
native kernel base in `RAX`, BootInfo in `RDI`, bootstrap CR3 in `RSI`, and top
of the allocated stack in `RSP`. The native `entry.S` now explicitly places
`stack_top` in `RDX` and `kernel_base` in `RCX` before calling C. The existing
BootInfo v1 remains byte-for-byte unchanged. BIOS/i386 sources are unchanged.

Following a successful PMM+IDT initialization, M1 constructs **one 2-MiB
high-half supervisor-only scratch virtual-memory window** at
`0xFFFF800000000000` (PML4 index 256). Slot zero deliberately stays unmapped.
There are 511 usable 4-KiB virtual slots, backed by **currently allocated
physical frames**, never by free or firmware-reserved frames. The frame must be
unmapped before it can be freed. Map and unmap invalidate the corresponding
TLB entry (`invlpg`). No paging entries are ever executable in the scratch
window (NX is mandatory). M1 fails closed if the CPU cannot enable NX. On the live CPU it also enables
CR0.WP so supervisor writes respect read-only PTEs.

Initialization allocates exactly 3 zeroed pages from PMM to construct the
new PDPT, PD and PT, then publishes their PML4 pointer **last**. Failed
allocations free partial tables and leave the PML4 unchanged. A built-in
runtime smoke test allocates one frame, maps slot 1 read/write+NX, writes
through the high-half alias, reads back via the identity address, unmaps,
then releases the physical frame.

## Strict boundaries

- These scratch mappings are **kernel/supervisor only**, not user-process
  mappings. No memory isolation exists between two applications.
- The bootstrap **0–4-GiB identity map remains RWX at 2-MiB granularity**.
  Therefore this does **not** establish global W^X, global NX, or full
  physical-memory protection. The scratch window alone enforces NX.
- This first VMM increment is single-CPU and non-preemptive. Do not enable
  hardware IRQs, SMP, APIC, USB stack, or user mode against it.
- Memory above 4 GiB is not allocated. The PMM does not automatically reclaim
  UEFI loader, BootServices or ACPI reclaimable pages.
- Exception handlers remain fatal; no recoverable page-fault handling.
- No heap, slab allocator, stack guard, copy-on-write or virtual address
  reuse policy is implemented beyond the scratch window.
- No kernel execution should use VMM mappings before they have been validated
  in QEMU. Intentionally faulting tests should run in QEMU only.

## Acceptance

1. `python3 apply_next.py --check` with unmodified Stage E, or with the
   Foundation patch already applied; both inputs are supported.
2. `python3 apply_next.py --apply`, once only (repeat is a no-op).
3. `make clean check` must report all prior tests plus the new VMM tests.
4. QEMU/OVMF UEFI display must include these additional lines:

```
IDT / TSS / IST: INSTALLED
PMM MANAGED PAGES: ...
PMM FREE PAGES: ...
PMM ALLOC/FREE: PASS
VMM HIGH-HALF RW/NX: PASS
```

5. BIOS `make run` must still show BIOS Foundation 0.4 and unchanged BootInfo
   ABI; this is a separate regression gate.
6. Physical Lenovo verification is a separate hardware gate and should
   follow successful host tests and QEMU confirmation.

None of these runtime conditions are asserted as completed by the patch
creation alone. This document is a specification and checklist until the
user observes and reports the required screen(s).
