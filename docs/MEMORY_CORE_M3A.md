# IntellectOS Memory Core M3-A — identity paging hardening

**Target branch:** `feature/c1-console`, commit `8c84db95044e0c22abc7805a911000fe1231ed13`.  
**Status:** candidate — offline compiler/host tests are not proof of QEMU execution.  
**Scope:** x86-64 UEFI, one CPU, IRQs disabled; BIOS Foundation 0.4 unchanged.

## Purpose and actual security perimeter

The preceding M1+M2 image uses a bootstrap 0..4GiB identity map of writable/executable 2MiB pages. Its high-half scratch VMM has NX, but a physical alias still permits execution and rewriting of the kernel. M3-A changes the existing identity map **after the M1/M2 smoke tests** without destroying the identity aliases needed by the physical allocator.

M3-A is only a transition toward protected per-process address spaces. It does **not** claim secure ring 3 isolation, DMA isolation, SMP coherence, recoverable faults or arbitrary physical memory protection against the kernel itself.

## Memory policy

- All present paging leaves have U/S=0 (supervisor-only).
- Kernel `.text` maps 4KiB RX; never RWX.
- Kernel `.rodata` maps 4KiB R+NX.
- Kernel `.data`, `.bss`, remainder of reserved image: RW+NX.
- Ordinary identity RAM and mapped MMIO: RW+NX with existing PWT/PCD bits preserved.
- Bottom 4KiB of the **UEFI-allocated stack** is unmapped as a guard. Remaining stack is RW+NX.
- The bounded high-half M1/M2 VMM stays unchanged; it was already supervisor-only and NX.
- CR0.WP and EFER.NXE must be enabled before publication; if not, fail closed.

## Implementation sequence and invariants

1. Use the original kernel-base register passed across the Stage E handoff. Confirm the linker's absolute runtime symbol address for `intellect64_entry` equals the physical kernel base; enforce `.text`, `.rodata`, `.data`, `.bss` page boundaries with linker symbols. `BootInfo v1` is untouched.
2. Inspect the exact loader bootstrap topology: PML4[0], 512 PDPT entries and present 2MiB PDEs. Validate supervisor-only, identity mapping, 2MiB page size, and no hugepage PAT bit. Require indices 0–3 to be present.
3. Select up to three 2MiB identity PDEs for splitting: first kernel page, last kernel-reserved page, and stack guard page. Deduplicate overlapping PDEs.
4. Allocate and fully populate the replacement 4KiB PTs from PMM. If *any* preparation fails, release all new PT frames and leave the existing page map unchanged. PMM-allocated PT frames are pinned permanently after commit.
5. Set NX on all other present 2MiB identity PDEs, publish the prepared PTs, reload CR3 once to invalidate non-global translations. Interrupts remain disabled; no other CPU uses the address space.
6. Verify all resulting identity leaves (not just samples), plus the stack-guard hole and kernel section boundaries. A failure stops the kernel before entering C1.

The fixed UEFI kernel reservation is 128 pages (512KiB). Bootstrap stack is 16 pages, with one lower guard page. In M3-A those bounds are intentional and must **not** be increased without simultaneous changes to the loader and PMM exclusions.

### Limitations to address before Ring 3

- Supervisor-only broad 0..4GiB identity memory remains mapped, although NX. Actual process spaces must have a different PML4 and must not receive a copy of unsafe supervisor aliases without review.
- **Identity writable alias for text is eliminated**, but kernel can still update page tables via the RW+NX physical identity mapping. That is not a safe privilege boundary against compromised ring 0.
- The PMM has no mapping refcounts; M3-A pins its new page-table pages indefinitely. A later address-space allocator must introduce owner/refcount/fencing rules.
- High physical memory, full 4KiB page-table management, executable mapping changes, SMP TLB shootdowns, demand paging, PCID and interrupt context switches are out of scope.
- This guards the **bootstrap** stack only; exception IST stacks are embedded in BSS and do not yet have their own guard pages.
- A torn/buggy page table or invalid transition will typically halt the kernel; recovery from memory corruption is not promised.

## Acceptance gates

1. `git diff --check` and `make clean check`: prior 35 host tests + new M3-A unit and static checks; no old test removed.
2. `tools/verify_kernel64.py`: entry at 0, no runtime relocations, no dynamic dependencies, flat image bounded; `.data` remains free of absolute pointer initializers.
3. `make uefi-m3-smoke` for an automated QEMU acceptance probe, or `make uefi-console` interactively: in QEMU, after M2 and before C1, console prints `M3-A IDENTITY W^X/NX: PASS`. Existing C1 commands `help`, `boot`, `mem`, `vmm`, `cpu`, `echo` still work. No Page Fault or exception.
4. Run negative-test mode in a **separate QEMU instance only**: attempt a write to RX text and a fetch from NX data; both must fault with the expected page-fault error bits (reserved for the next package, not currently implemented).
5. Ensure BIOS loader image and BootInfo v1 record definitions remain unchanged. Preserve `feature/c1-console` checkpoint. Only after positive and negative runtime gates, consider M3-A fully verified.

## Next steps

M3-B: managed address spaces and safe table ownership/refcounts; M3-C: guarded stacks and recoverable user faults; C2: optional GOP + real headless firmware path. Ring 3 and `.ivx` must wait until those gates pass.
