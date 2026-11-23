# Documentation map

**Updated:** 2026-11-23

This file is the entry point for the repository documentation. A document's
presence does not mean the described feature is implemented. Read the status
at the top of each document and use the evidence sections below.

## 1. Current implementation evidence

| Area | Document | Status |
|---|---|---|
| Native boot and ABI | [ARCHITECTURE.md](ARCHITECTURE.md), [BOOTINFO_V1.md](BOOTINFO_V1.md), [UEFI_STAGE_E.md](UEFI_STAGE_E.md) | BIOS 0.4 and Stage E runtime observations recorded |
| Verification history | [MILESTONES.md](MILESTONES.md), [RELEASE_BASELINE.md](RELEASE_BASELINE.md), [HARDWARE_TEST.md](HARDWARE_TEST.md) | Mixed runtime/host evidence; no blanket certification |
| Memory Core M1–M3-A | [MEMORY_CORE_FOUNDATION.md](MEMORY_CORE_FOUNDATION.md), [MEMORY_CORE_M1.md](MEMORY_CORE_M1.md), [MEMORY_CORE_M1_VERIFICATION.md](MEMORY_CORE_M1_VERIFICATION.md), [MEMORY_CORE_M2.md](MEMORY_CORE_M2.md), [MEMORY_CORE_M3A.md](MEMORY_CORE_M3A.md) | M1 observed; M2/M3-A implementation and host tests, runtime pending unless separately recorded |
| Console and hardware | [CONSOLE_C1.md](CONSOLE_C1.md), [HARDWARE_H1_H2.md](HARDWARE_H1_H2.md), [STORAGE_S0_READONLY.md](STORAGE_S0_READONLY.md) | Source and host-test coverage; runtime evidence varies |
| User and installer foundations | [BOOT_MENU_B1.md](BOOT_MENU_B1.md), [LIVE_USB_L1.md](LIVE_USB_L1.md), [INSTALLATION_FOUNDATION_I2.md](INSTALLATION_FOUNDATION_I2.md), [USER_TERMINAL_U1.md](USER_TERMINAL_U1.md) | Bounded research/prototype features, not a general OS |
| Future architecture and roadmap | [CONSOLE_FIRST_ROADMAP.md](CONSOLE_FIRST_ROADMAP.md), [ROADMAP.md](ROADMAP.md) | Ordered implementation plans and acceptance gates |

## 2. Status vocabulary

- **Runtime-observed** — the behavior was observed in QEMU/OVMF or on a named
  physical machine. It is not a portability or security certification.
- **Host-tested** — source-level and host/unit checks pass; this is not
  evidence of ring transitions, real firmware behavior or real hardware.
- **Candidate** — implementation exists but acceptance gates remain incomplete.
- **Design draft** — normative or exploratory specification without a matching
  implementation; no claim of completion.
- **Historical** — records a prior patch, checkpoint or uploaded artifact.

## 3. Native implementation documentation

Start with [the architecture overview](ARCHITECTURE.md), then follow:

1. [BootInfo ABI v1](BOOTINFO_V1.md)
2. [UEFI Stage E](UEFI_STAGE_E.md)
3. [Memory Core foundation](MEMORY_CORE_FOUNDATION.md)
4. [M1](MEMORY_CORE_M1.md), [M1 verification](MEMORY_CORE_M1_VERIFICATION.md),
   [M2](MEMORY_CORE_M2.md), [M3-A](MEMORY_CORE_M3A.md)
5. [C1 console](CONSOLE_C1.md), [H1/H2 hardware](HARDWARE_H1_H2.md),
   [S0 storage](STORAGE_S0_READONLY.md)
6. [B1 menu](BOOT_MENU_B1.md), [L1 live USB](LIVE_USB_L1.md),
   [I2 installation](INSTALLATION_FOUNDATION_I2.md),
   [U1 terminal](USER_TERMINAL_U1.md)

## 4. Product specifications and proposals

These documents define intended future systems and must be read separately
from the native implementation evidence:

| Document | Topic | Status |
|---|---|---|
| [01_INTELLECTOS_SYSTEM_ARCHITECTURE.md](01_INTELLECTOS_SYSTEM_ARCHITECTURE.md) | future OS architecture | design draft |
| [02_INTELLECTOS_INTELLIGENCE_LAYER.md](02_INTELLECTOS_INTELLIGENCE_LAYER.md) | AI and autonomy layer | design draft |
| [03_VIBELANG_LANGUAGE_SPEC.md](03_VIBELANG_LANGUAGE_SPEC.md) | `.vibe` language | design draft |
| [04_AI_COMPILER_ARCHITECTURE.md](04_AI_COMPILER_ARCHITECTURE.md) | compiler pipeline | design draft |
| [05_IVX_BINARY_EXECUTABLE_SPEC.md](05_IVX_BINARY_EXECUTABLE_SPEC.md) | `.ivx` format | draft plus selected implementation foundations |
| [06_VSG_VBIR_INTERMEDIATE_FORMATS.md](06_VSG_VBIR_INTERMEDIATE_FORMATS.md) | VSG/VBIR | design draft |
| [07_OBJECT_LIBRARY_PACKAGE_DEBUG_FORMATS.md](07_OBJECT_LIBRARY_PACKAGE_DEBUG_FORMATS.md) | `.ivo`, `.ivl`, `.ivpkg`, `.ivdbg` | design draft |
| [08_RUNTIME_ABI_LOADER_AND_IPC.md](08_RUNTIME_ABI_LOADER_AND_IPC.md) | processes, ABI and IPC | design draft |
| [09_SECURITY_SUPPLY_CHAIN_AND_UPDATES.md](09_SECURITY_SUPPLY_CHAIN_AND_UPDATES.md) | trust, updates and supply chain | design draft |
| [10_IMPLEMENTATION_PLAN.md](10_IMPLEMENTATION_PLAN.md) | implementation dependencies | roadmap/draft |
| [11_TESTING_CONFORMANCE_AND_BENCHMARKS.md](11_TESTING_CONFORMANCE_AND_BENCHMARKS.md) | conformance program | draft until tests are implemented |
| [IntellectFS_v1_FULL_ARCHITECTURE_AND_DISK_FORMAT.md](IntellectFS_v1_FULL_ARCHITECTURE_AND_DISK_FORMAT.md) | IFS v1 format | project specification |

## 5. Repository invariants

1. BootInfo ABI v1 is 152 bytes of header plus 24-byte memory records.
2. BIOS and UEFI remain separate native entry paths.
3. Build/install operations must write only under `build/`; do not treat an
   image builder or flashing script as a safe disk-write API.
4. `.vibe` is source, `.ivx` is executable, and neither is a substitute for an
   implemented toolchain or loader.
5. AI is not part of the trusted boot, signature or memory-isolation path.
6. An implementation document supersedes an older roadmap date or patch
   description only when it records its own verification evidence.

## 6. Validation commands

```bash
make clean check
make uefi-console
make uefi-m3-smoke
make uefi-hw-smoke
make uefi-s0-smoke
make uefi-live-smoke
make uefi-u1-smoke
make uefi-ifs-smoke
```

`make clean check` is the baseline host/image gate. Smoke targets require the
matching acceptance evidence and must be reported as pass/fail/not-run rather
than inferred from compilation.
