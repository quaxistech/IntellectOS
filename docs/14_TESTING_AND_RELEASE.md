# Испытания, совместимость и выпуск IntellectOS

## 1. Не смешивать evidence classes

| Status | Достаточное доказательство | Недопустимая подмена |
|---|---|---|
| `DESIGNED` | точная спецификация | не `implemented` |
| `HOST TESTED` | тест host с stdout/exit=0 | не реальная загрузка |
| `QEMU TESTED` | runtime OVMF/BIOS с serial/screens + hash | не «работает на любом ПК» |
| `HARDWARE OBSERVED` | конкретное устройство, log/screens | не массовая совместимость |
| `FUZZ/NEGATIVE PASSED` | scoped inputs, budget, corpus | не proof of absence of bugs |
| `SECURITY REVIEWED` | независимый audit | не гарантированная безопасность |
| `FORMAT FROZEN` | byte fixtures+readers+strict parsers | не номер v1 в имени |
| `RELEASED` | all release gates below | не green `make check` |

Описания старых тестов M1/C1/U1 в `history/` являются историческими свидетельствами, не текущим CI state. Для любой новой версии указывать Git commit, environment, exact commands, output artifact hashes and test date. Нельзя копировать старые PASS в новую таблицу без прогонов.

## 2. Неразрушающие host gates

`make clean check` (if supported by current Makefile); standalone `python3 -m unittest discover -s tests -v` и `host_tests`; freestanding clang/lld with `-Wall -Wextra -Werror`; ELF image check no unintended relocation; PE32+ signature, correct EFI machine/subsystem; memory/BootInfo ABI static asserts; link/runtime bounds; GPT independent primary+backup CRC; FAT32 EFI byte-by-byte readback; IFS bootstrap/layout/seal/data integrity; IVX cryptographic signature and policy; root config `argon2` and timezone tests; Vibelang tokenizer/verifiers as they exist.

Test CLI must not require interactive password in CI or write `/dev/*`; use tmp images and synthetic identity. Missing dependency should fail with actionable message **before** launching long kernel builds.

## 3. QEMU/OVMF integration gates

1. OVMF cold boot EFI, direct native kernel after EBS (not EFI-only splash).
2. PMM/GDT/IDT/TSS/VMM/M3 protection and bounded fault handling.
3. Ring3 Terminal U1 `write/read/clear/mem/help/exit`, kernel fallback.
4. IFS-1 system image attached as sole HDD, native read root and trust check, terminal **from file**, not embedded fallback silently.
5. Installer two-step: boot USB installer, select writable blank virtual disk, write partitions, remove USB completely, reset UEFI variables as documented, boot solely from HDD and use terminal.
6. Vary QEMU disk controller: PIIX read-only baseline, AHCI, NVMe, VirtIO when driver exists; invalid GPT/IFS, corrupt IVX, absent disk, small disk, wrong sector size, timeout.
7. Vary GOP resolution, pitch, color order and headless mode; terminal keyboard via PS/2 then xHCI, mouse only after native driver gate.
8. Model update failure: corrupted new boot slot/revoked IVX but known-good old root recovered.

**No fake positive:** printed `PASS:` by Python from an image check is not equivalent to output from native ring3 process. Smoke verifies actual kernel state and user input.

## 4. Adversarial storage testing

Crash injector cuts power after each CoW allocation, extent write, metadata block, descriptor write, Flush/FUA, seal, bootstrap switch. Mount sees previous or new verified root, never mixed. Block device may return short read/write, stale sector, reordered writes, torn 4KiB, wrong 512/4096 geometry. Distinguish accepted transaction, unknown commit status and retry idempotence. Full disk and GC fencing tests; deterministic repair path. Run on fake disk/files first, then disposable physical devices only after gate.

## 5. Security and format testing

- Fuzz BootInfo, GPT, FAT32, IFS bootstrap, nodes/extents/dirs/seals, IVX headers/segments/manifest/signature envelope, user IPC and user ptrs.
- ABI alignment, unsigned overflow, malformed enum, reserved bits, unknown required extensions, overlapping buffers, cycles and deep recursion.
- Ed25519/BLAKE3 official vectors and independently implemented parser results, no trusted key by file-provided name alone.
- W^X aliases, user/kernel separation, two process CR3, exception cleanup and stale handles.
- Vibelang grammar/type/effect/borrow/SSA/reloc tests, deterministic build and binary identity.
- AI prompt injection/capability broker L0..L5, network disconnect, model unavailable and strict fallback.

## 6. Производительность

Record cold boot time, shell ready, memory foot print, syscall p50/p95/p99, process start and IVX link cost, IFS sequential/random/small-file fsync, WAF and 99.9p latency, compile time and peak RSS, energy where instrumentation exists. Use same CPU/memory/storage/flags workload; no unsupported speed percentages vs ext4/Linux, LLVM or other systems. Fast-path must not break correctness or verified page ownership.

## 7. Physical acceptance and release matrix

Primary test machine Lenovo IdeaPad AMD A6-9225 is disposable test target; success there verifies **that machine only**. Additional vendor/CPU/firmware profiles required for general support: modern Intel+AMD UEFI, NVMe and SATA, USB-only keyboard, various GOP modes, USB boot quirks, secure boot policy, memory sizes, headless serial. Document unsupported/unsupported with clean refusal. Never automatic write to developer Ubuntu host disk.

## 8. Release thresholds

**Terminal Alpha:** clean source build + host/QEMU, no destructive installer escapes, true IVX-from-IFS, root config/login, selfboot after USB removal on at least one test PC. **Beta:** IFS RW+recovery, native AHCI/NVMe and USB HID, usability/partition editor tests, signed updates, multiple platforms. **Stable:** security audit, fuzz convergence, independent formats readers, signed releases, recovery drill, explicit supported hardware matrix, reliable error reports and accessibility. **Desktop/AI** separate optional release tracks.
