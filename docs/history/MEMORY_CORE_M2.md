# Intellect OS — Memory Core M2: bounded Kernel Heap

## Status

This is a **candidate implementation** following the M1 D2 baseline
(`9367458e2e0f03f5e045a3683153714251643ea1`). The predecessor M1 D2
was observed running on QEMU/OVMF and Lenovo IdeaPad. M2 is not described as
QEMU- or hardware-verified until separately observed.

## Ownership and constraints

The heap consumes virtual slots 32..287 in the existing M1 high-half VMM
(PML4[256]). This reserves 256 × 4-KiB = 1 MiB of non-executable,
supervisor-only RW aliases. Slots 0 (guard), 1 (M1 test), 2..31 and 288..511
are **not** owned by the heap. No other kernel component may map or unmap
heap-owned slots. The heap fails closed if its page-table mappings have been
changed unexpectedly; it does not yet enforce slot ownership inside VMM.

Backing physical pages are obtained exclusively from the M1 PMM and zeroed
through the bootstrap identity mapping **before** being attached to the VMM.
The heap never grants execute permissions. Empty pages are unmapped before
returning their frames to PMM. Every allocated page is tracked in static,
non-allocating metadata within the kernel image. Initialization does not
allocate pages and is rejected if any reserved slot is already mapped.

**Restrictions:** one CPU, interrupts disabled, no scheduling, no reentrancy,
no hardware IRQ handlers, no SMP, no user mode. The bootstrap low-4-GiB
identity mapping remains broadly RWX; the high-half heap's NX bits **do not**
constitute global W^X protection. This heap is not a security boundary.

## API

Declared in `kernel/include/kheap64.h`:

- `intellect_kheap64_init()` — one-time initialization after PMM/VMM ready.
- `intellect_kheap64_alloc(size, alignment)` — accepts 1..262144 bytes,
  power-of-two alignment 1..4096; returns zero-initialized storage or NULL.
- `intellect_kheap64_free(ptr)` — accepts the **exact start address** of a live
  allocation. Rejects unallocated/interior/foreign/double-free pointers.
  Memory is scrubbed before release, and fully empty slabs are reclaimed.
- `intellect_kheap64_stats()` — active pages, high-water pages, outstanding
  allocations and **rounded capacity**, not requested payload bytes.
- `intellect_kheap64_self_test()` — bounded runtime smoke test.

Small allocations use classes 32, 64, 128, 256, 512, 1024 and 2048 bytes.
A 32-byte slab has 128 objects. Large objects use 1..64 **virtually
contiguous** 4-KiB pages; underlying physical pages need not be contiguous.
The backing frame may not be unmapped or released by other code. No raw
address-space switch or firmware service call occurs in heap operations.

When PMM or the fixed heap window is exhausted, allocation fails without
expanding the window. Partial large-allocation mapping failures roll back
already attached pages; a failed invariant marks the allocator poisoned and
blocks subsequent operations. The allocator neither silently extends into
reserved memory nor modifies BootInfo v1.

## Acceptance and evidence

1. Clean and expected M1 D2 `main` at commit `9367458e...`; installer
   `--check` succeeds without writing.
2. Apply with `--apply`; inspect `git diff` (no BIOS/UEFI/BootInfo changes).
3. `make clean check` passes all existing tests plus host heap tests with both
   `-O0` and `-O2`, and the ELF verification gate still passes.
4. QEMU/OVMF reaches `KHEAP SLAB/PAGE-RUN: PASS`, without exceptions.
5. BIOS regression (`make run`) still boots BIOS Foundation 0.4.
6. Only after these gates, test UEFI USB on Lenovo; no physical disk writes.
7. Commit/push to `main` only after verified completion.

Host tests exercise zeroing, class boundaries, aligned objects, multiple
concurrent slabs, page runs, exhaustion, exact-pointer validation, double
free, reclamation, accounting and tampered PTE detection. The host VMM test
uses a simulated address window and is **not** equivalent to executing a
real `invlpg` or CPU NX enforcement.

## Next stages

A production kernel heap requires verified virtual-address ownership leases,
page-table lifetime/rollback policy, guard pages, fault-injection testing,
concurrency/synchronization and stronger physical/virtual isolation. Dynamic
code generation is not permitted against today's broad identity mapping.
