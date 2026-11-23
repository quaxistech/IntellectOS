# IntellectOS — console-first / server-first engineering roadmap

**Status:** historical architecture decision updated 2026-11-23. It fixes implementation order and acceptance criteria, but later checkpoints (M2, M3-A, C1, H1/H2, S0, B1, I2 and U1) have changed the live baseline. Use [MILESTONES.md](MILESTONES.md) for recorded evidence.

## 1. Product principle

One IntellectOS kernel, one process/user ABI, one `.ivx` executable format. The service sets vary:

- **Base** — kernel, mandatory init, console transport, policy, process manager, VFS, validated IVX loader and diagnostic logging.
- **Server** — Base + networking, remote terminal service, storage, package/update manager, service supervisor, crash recovery, optional AI services.
- **Desktop** — Base + graphics compositor, input stack, fonts, desktop shell and optional AI frontend. Server and Desktop components can coexist.

No use of graphical shell is required for startup, package management, debugging or operational recovery. The future headless UEFI boot path must work when GOP is unavailable; the current Stage E does NOT support that yet.

## 2. Distinguish kernel monitor, TTY and shell

**Early monitor (C1):** polling UART after PMM/VMM self-test; kernel-embedded commands, no arbitrary program execution. Its purpose is to eliminate repetitive USB-flash testing and recover diagnostics in QEMU.

**TTY:** a driver/service abstraction for input bytes, line discipline, scrollback, encoding and multiple endpoints (COM1, framebuffer keyboard, future PTYs and remote sessions). Must not assume ANSI terminal escapes for every endpoint. Raw terminal input is untrusted; define bounded queues and error policy.

**Shell:** an ordinary user-mode `.ivx` process. Command parser, cwd, environment, commands, pipes, redirection, process creation and job control are user-space semantics. Shell is not the kernel and has no unconditional device rights.

## 3. Ordered milestones with gates

### C1 — diagnostic serial console (this patch)

- COM1 polling I/O, runtime UART probe, bounded TX, line parser, memory and boot inspection commands.
- Two QEMU targets, headless virtual console and QEMU GUI with serial stdio.
- Host command parser tests and existing boot/PMM/VMM regression suite.
- Done when `make clean check` succeeds and a live QEMU session accepts `help`, `boot`, `mem`, `vmm`, `echo` and `halt` without USB. Not claimed done merely by compiling sources.

### C2 — boot diagnostics without requiring framebuffer

- Early serial logging from the UEFI loader where possible, with timeout and fallback policy.
- BootInfo video_kind NONE is valid if serial or another approved debug transport is available; maintain v1 header size/offsets.
- Refactor kernel diagnostic `console_t` to bounded transport-neutral output; no blind writing to framebuffer_addr=0.
- QEMU `-vga none` / no GOP and standard GOP must both boot the same kernel. Physical devices without serial must not lock forever; provide clear failure/recovery policy.
- Failure injection: BootInfo malformed, no GOP, no UART, PMM failure, exception report after ExitBootServices.

### M2 — bounded kernel heap

- The M2 implementation exists in the current source and has host coverage.
  QEMU/Lenovo runtime verification remains a separate acceptance gate.
- Page ownership, size-class/slab or buddy policy, alignment, allocator metadata integrity, zero-init and OOM rollback, guard pages for critical stacks.
- No reclaiming allocated frames still mapped. Fail-closed tests for double-free, invalid free, integer overflow and reentrancy policy.

### M3 — protected address spaces

- Global kernel W^X, NX, read-only text, physical aliases controlled, page table ownership, kernel heap mapping, zeroed user pages, address spaces with PCID/INVPCID policy if supported.
- Bootstrap low 4-GiB identity mapping currently RWX; redesign must avoid a transient alias that lets user code write kernel RX pages.
- Build a deterministic page-fault recoverability path with emergency stacks and independent panic logging.
- Tests: attempt to execute NX, write RX, access supervisor pages from ring3 and free still-mapped pages. First intentional faults in QEMU only.

### U1 — user mode and syscalls

- Minimal ring3 entry with valid SS/RSP, TSS.RSP0, SWAPGS policy, privilege transition (safe `iretq` bootstrap), conservative interrupt return.
- Minimal syscall ABI: write(handle, buf, n), exit(status), memory/map (capability-controlled); process rights, cancellation and numeric error codes.
- Verify user pointers and page access; invalid pointers must not panic the kernel. No keyboard input in ring0 shell once userspace is available.

### U2 — multitasking and time

- APIC/PIT/HPET choice based on verified timer configuration, clock monotonicity; bounded scheduler, context switch, independent kernel stacks, process teardown.
- Two processes must execute interleaved, have isolated memory, fault independently, and release resources at exit.

### F1 — IVX minimal executable

- Freeze minimum executable subset with `IVXHeaderV1` (128 B) and `IVXSegmentV1` (64 B), offline parser and independent verifiers, official test vectors, negative fuzz corpus.
- Trusted IVX loader: initially static user PIE without shared libraries, zero BSS, separate RX/RO/RW pages, entry and stack validated.
- Only then construct the first genuine `.ivx` command `hello.ivx`, using a small interim host builder until Vibelang compiler reaches this target.

### S1 — VFS + terminal service

- In-memory VFS with canonical paths, ownership, lookup, fd/handle operations and no asynchronous disk writes; serial console accessed through a TTY API.
- Device-independent framebuffer/PS2/USB HID later; command shell runs without GUI.
- Minimal `.ivx` commands: `help`, `echo`, `pwd`, `ls`, `cat`, `mem`, `ps`, `kill` with explicit permission checks. The `ls` of C1 must not falsely pretend that a filesystem exists.

### S2 — persistent storage + server baseline

- Read-only disk support first; then a tested writable filesystem with crash-consistent journal, page cache, driver isolation and separate installer.
- Network stack, scoped sockets/ports, SSH-equivalent authenticated remote terminal (not hand-rolled cryptography), service lifecycle and logs.
- Server release runs over serial/network without framebuffer. Graceful shutdown, safe upgrades and recovery images are required.

### D1 — GUI layer

- Graphics runtime as ordinary non-kernel component, device and event interfaces with explicit permissions, compositor, UI toolkit, window isolation and optional desktop shell.
- A crashed compositor must not destroy the base OS or prevent recovery in console mode.

### A1 — intelligence layer

- AI models are user processes/services, not trusted parsers or schedulers. Permission broker, audit, resource quotas, observational mode by default.
- AI compiler translates Vibelang `.vibe` → VSG → VBIR → `.ivo` → `.ivx` via deterministic verifier, with no requirement to run AI at program startup.
- Disabled or unavailable AI MUST NOT prevent boot, shell, networking or package management.

## 4. Development workflow

1. Change code only in `/home/quaxis/IntellectOS`; no parallel debug clone as the source of truth.
2. Clean build and host tests, inspect `git diff --check`, `git status`, LLVM relocation/image verification and QEMU boot.
3. Regular QEMU serial/GUI regressions; hardware Lenovo only at meaningful milestones such as boot mode, memory mapping, ring3 hardware interrupts, storage and input support.
4. Every milestone has independent evidence `PASS`/`FAIL`/`NOT_RUN`. A crash/silent terminal is not a pass.
5. GitHub main contains confirmed checkpoints; keep incomplete work on a local branch or packaged patch. Never rewrite `main` history or commit unverified devices/secrets.
6. Installation images built under `build/` only, no implicit `dd`/block-device writes.

## 5. Single source-of-truth invariants

- BootInfo ABI v1 stays 152-byte header + 24-byte records. Console/IVX ABI do not reuse or reinterpret it.
- No new `.vbin`; `.vibe` source, `.ivx` application, `.ivl` library, `.ivo` object.
- BIOS Foundation 0.4 regressions continue while UEFI x86-64 becomes primary development path.
- Early C1 runs in ring0 only as a transitional monitor, and eventually is superseded by user-space `.ivx` shell.
- Fixed resource/architectural limits must be measured under QEMU and physical system; do not claim performance superiority from terminology.
