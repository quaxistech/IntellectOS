# Freeze-register: незавершённые форматы, точные байты и acceptance

Цель — первая совместимая версия `v1` без фиктивной истории миграций. `DRAFT` означает, что не хватает конкретных byte-level контрактов; `BOOT PROFILE` фиксирует узкий реализованный subset; `FROZEN` назначается только после golden fixtures и независимых readers. **Не повышать версию из-за очередной редакции Markdown.**

## 1. IntellectFS-1

**Утверждено:** 4096B disk metadata block, 4 bootstrap copies, BLAKE3, CoW, stable OID, extents, seal publish, descriptor/root selection, allocator/GC safety, flush semantics/rollbacks, recovery/repair, snapshots and capability VFS. **Initial wire profile** описан в §27.3 полной спецификации; host formatter+strict native reader существуют.

**Нужно заморозить перед полным production RW:** exact field offsets for all required object/directory/extent/allocator records; node ordering, variable length canonical encoding; all feature bit registry; key/encryption nonce and digest domains; multi-shard commit and merge consistency; seal ring wrap/fence/epoch, power loss linearization and device block size policy; GUID constant byte representation; independent two parsers and tests for corrupt, torn, reordered writes; fsck inspect and repair scope. Crash test should include `write` reported success with later loss if dishonest controller — never claim impossible guarantees.

## 2. IVX-1

**Утверждено:** 128B header, 64B segment, 32B directory, BLAKE3 Merkle 16-ary, masked hash field, detached IVXS signature envelope, Ed25519 initial and future algorithm registry, EXEC/SHARED distinction, immutable TEXT and non-privileged manifest. **Limited developer boot** uses exact BLAKE3 pin; no general in-kernel signature trust store.

**Нужно заморозить:** complete canonical manifest/ABI/ImportRecord/ExportRecord field enum/tag/offsets, dictionary identities/SignatureId, rights reference registry, fixup/RELR/CHAIN ownership and branch coverage, TLS/init DAG/function fragments/unwind IDs, CPU feature variants, LoadPlan envelope, ClosureRecord and trust-store release policy; canonical IVXS signing domain and key-id representation, antireplay, downgrade failure and import graph cycles; two independent readers/golden byte fixtures and fuzz corpus. No accept arbitrary signed `.ivx` in runtime until G5.

## 3. Vibelang/VSG/VBIR/IVO/IVL/IVPKG/IVDBG

Language 1 grammar/effects/ownership semantics are draft. VSG/VBIR proposed 64-byte enveloped tables, typed SSA, full source identity; magic escaped sequences and ordinal tags need binary fixtures. `IVOHeaderV1` 64B proposed, `.ivl` image_kind SHARED, `.ivpkg` signed chunk-index and `.ivdbg` detached identity. Before freeze: unique enum registry, import/export ABI, reserved bit behavior, canonical UTF-8/normalization rules, error semantics, cross-machine build identity reproducibility, reader/parser roundtrip and negative tests. `.vibe` and IR never executable by kernel.

## 4. Runtime/User ABI/IPC

BootInfo ABI existing and fixed by code (`152B/24B`) is independent. U1 `int 0x80` 1..5 is bootstrap syscall set, not final User ABI. Must define process entry alignment, ABI calling convention, 64-bit handle generation+rights, canonical IPC envelope/version, list of syscall IDs/errors, thread/TLS, cancellation/backpressure and fault semantics. Cannot make incompatible late change without documented compat and conformance proof after freeze.

## 5. Installer/root/timezone

Root password decision stable: optional, 0..256 UTF-8, Argon2id when stored as verifier, no fallback on invalid user record. Need exact runtime on-disk `/etc/system.json` schema with canonical encoding and bounds, `init`/login session authority and timezone behavior. GPT/ESP are external stable standards; IntellectFS GPT type GUID must be fixed across formatter and kernel. No hidden Linux system partition masquerading as IFS.

## 6. Freeze test protocol

1. Specify all byte offsets/reserved/flags/enums and bounds in **existing v1 spec**.
2. Produce canonical golden fixtures for minimal and maximal bounded inputs plus corruption variants.
3. Independent writer and two independent strict readers agree on bytes/content root.
4. Differential fuzz and explicit adversarial fixtures; no ambiguous decode.
5. Migration/backward-compatibility rules are unnecessary before first stable v1; do not create them prematurely.
6. Test full integration including actual QEMU/OVMF boot and physical disk when permitted.
7. Record exact commit SHA, toolchain, fixture hash, status as FROZEN, and lock the registry.

## 7. Registry status 2026-11-23

| Item | Status | Reason |
|---|---|---|
| BootInfo v1 | CODE ABI EXISTS | definitions and tests, no arbitrary consumer changes |
| IFS system boot profile | BOOT PROFILE | formatter + native reader; not full RW |
| Full IFS transactional RW | DRAFT | crash/GC/multishard/record freeze pending |
| IVX pinned one-file boot | BOOT PROFILE | pin and builder, not generic trust store |
| Full IVX-1 executable format | WIRE DRAFT | nested table fixtures/complete loader pending |
| User ABI | PARTIAL | U1 5 syscalls, not full process manager |
| Vibelang, VSG/VBIR, IVO/IVL/IVPKG | DESIGN/WIRE DRAFT | no native full compiler in main |
| Root/login/session | PARTIAL | stored host config, not verified native login |
| Firmware installer | PARTIAL/TESTABLE | code exists; hardware acceptance separate |
