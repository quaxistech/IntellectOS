# IntellectOS — единая архитектура системы

**Статус:** архитектурный контракт и разделение ответственности.

## 1. Слои системы

```text
UEFI / Legacy BIOS / будущие platform-specific firmware adapters
     ↓ validated boot handoff (BootInfo ABI)
Kernel TCB: bootstrap, MMU/PMM/VMM, scheduler, IPC, syscall and capabilities,
            interrupts/timers, process isolation, device access boundaries
     ↓ typed handles, validated buffers, bounded operations
System services: init/supervisor, device manager, VFS, IntellectFS,
                 Verified Install Manager, login, TTY, networking, package manager
     ↓ User ABI + capability broker + verified IVX execution
Userland: terminal.ivx / utilities / server services / desktop compositor
     ↑ developers: Vibelang → VSG → VBIR → IVO → IVX, optional AI advice
```

Код ядра не содержит языкового компилятора, модели ИИ, полного package resolver или многообразных GUI-плагинов. Firmware Boot Services после `ExitBootServices` недоступны. Установщик, пока работает в UEFI, может использовать стандартные firmware disk/keyboard/GOP services **до** ExitBootServices; это не создаёт нативных runtime-драйверов.

## 2. Границы доверия

- **Firmware** сообщает memory/device/video snapshot, но все размеры, адреса и указатели проверяются; firmware handles не переходят в native user/kernel.
- **Kernel** отвечает за процесс/поток, CR3, W^X, CPU exceptions, capability handoff, DMA/IOMMU и безопасный syscall; плохой парсер приложения не должен работать с привилегиями ядра.
- **Install Manager** вне ядра проверяет IVX, подписи/ABI/импорты, подготавливает immutable package closure и bounded `LoadPlanV1`.
- **Filesystem** проверяет CoW roots, seal, BLAKE3, OID, transaction epochs; приложению выдаётся файловый handle, а не доступ к блочному устройству.
- **AI-agent** формирует предложение с входным revision hash, diff, требуемыми правами и тестовым планом; policy broker решает, разрешено ли действие.

## 3. Системные сервисы

| Подсистема | Назначение | Первое безопасное состояние |
|---|---|---|
| `boot` | firmware bridge, firmware exit, BootInfo | UEFI/BIOS success/fail diagnostics |
| `memory` | PMM/VMM/heap, ownership | one CPU, bounded, NX/RO protection |
| `process` | ring3, process table, scheduler, exit/wait | one isolated terminal, then multiple CR3 |
| `syscall/IPC` | bounded typed requests, handles | known syscall IDs, rights checked |
| `hardware` | ACPI/PCI/device discovery | identify != supported driver |
| `block` | AHCI, NVMe, USB MSC, VirtIO, PIIX as per adapter | read-only first, write admission later |
| `VFS/IFS` | path/OID, mounts, transactions | sealed RO system root; then durable RW |
| `login/policy` | user principal, optional password, capabilities | explicit empty-password vs hashed-password |
| `exec` | IVX verification and user mapping | one pinned boot profile; then generic signed loader |
| `tty` | COM1, framebuffer, PS/2, USB HID, pty | no graphical dependency for server |
| `packages/updates` | immutable store, closure, slots | atomic install/rollback and recovery |
| `AI` | health/developer/performance agents | no access without broker; optional runtime |

## 4. System lifecycle

**Cold boot:** firmware → verified loader → BootInfo → kernel init → hardware registry → safe storage attach → mount sealed IFS root → trust policy → `init` → identity/session setup → process spawn via VerifiedArtifactHandle → terminal/server/desktop. On failure, report serial/fb status and attempt bounded recovery; no silent unverified fallback to arbitrary executable bytes.

**Normal process start:** user requests executable; Install Manager resolves closure, validates exact signed bytes/permissions, kernel maps page-protected content in new address space; process gets restricted handles and control at ABI-defined entry. No elevated privilege from image metadata alone.

**Update:** write new files/root/closure as transaction, flush and seal, publish atomic generation, reboot/new spawn sees new version, old reference holders retain old generation; rollback leaves previous sealed root.

## 5. Universal computer support as engineering policy

Boot path depends on real firmware and CPU capabilities; device adapters advertise supported versions/sector sizes/queue features. Unsupported: show meaningful reason, use fallback or recovery path; do not pretend success. Multiple displays, rotated monitors, mixed pixel formats, NVMe/AHCI/USB, legacy BIOS and UEFI are capability profiles, not assumptions all work today. Single kernel architecture is goal across **editions**, distinct architecture ports have explicitly versioned ABIs.

## 6. Performance and resilience

- Boot in bounded stages, no infinite unbounded firmware enumeration.
- Limit heap/stack/IRQ handling; measure P95/P99, cold/warm startup, memory overhead and storage write amplification.
- Time source monotonic for scheduling; RTC/timezone for presentation, never for nonce uniqueness or log causal order.
- No AI/model/network requirement for boot or execution. Inference may propose memory layout or scheduling policy, but trusted kernel finalizes and enforces limits.
- Safe degraded modes: serial-only, low-resolution framebuffer, no audio, optional minimal input, read-only filesystem and recovery console when supported.

## 7. Scope discipline

First produce an end-to-end installed terminal boot whose IFS, IVX and identity paths are real and tested; then extend to multi-process, native hardware, safe partition editor, network/server and graphical UI. The installed system must not be marketed as production-ready on the basis of a one-file pinned IVX and read-only PIIX alone.
