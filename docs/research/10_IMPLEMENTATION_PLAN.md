# Дорожная карта реализации IntellectOS, Vibelang, ИИ-компилятора и IVX

**Редакция:** 1.0 DRAFT · **Дата:** 2026-11-20 · **Принцип:** доводим каждый слой до проверяемого рабочего состояния, не меняя BootInfo ABI v1 без отдельно доказанной необходимости и не объявляя архитектурные планы готовой системой.

## 1. Фактический checkpoint

По GitHub `quaxistech/IntellectOS` существуют BIOS/i386 и UEFI/x86-64 ветки, Stage E, 152-byte BootInfo v1, 24-byte memory descriptors и экспериментальный Memory Core M1. Отчёт 19.11.2026: 31 автоматический тест; QEMU/OVMF и физический Lenovo IdeaPad с PMM, IDT/TSS/IST, high-half scratch VMM RW/NX. **Ограничение:** bootstrap identity map RWX, нет полноценного user/kernel isolation, user processes, scheduler, файловой системы общего назначения, компилятора Vibelang, загрузчика IVX или runtime AI. Это Stage E/M1, а не «готовая интеллектуальная ОС».

## 2. Общий roadmap и critical path

```
BOOT FOUNDATION (реализовано частично)
       ▼
KERNEL MEMORY HARDENING
       ▼
USER MODE / SYSCALL / PROCESSES
       ├───────────► IVX signed minimal executable loader
       │                        ▼
       │                 .ivl imports / closure / TLS / IPC
       │                        ▼
       │                     OS user tools
       ▼
DRIVERS / VFS / PKG / TRUST / LOGGING
       │
       └───────────► AI SERVICES после устойчивой OS policy

Параллельная HOST цепочка:
LANGUAGE PARSER → VSG → VBIR → X86-64 CODEGEN → .ivo → IVX LINKER/VERIFIER
                                                      │
                                                      ▼
                                                IVX USER LOADER
```

**Критический параллелизм:** строить форматный parser и host cross compiler можно ДО появления процессов. Нельзя «запустить `.ivx` в IntellectOS» до ring3, syscall и VMM безопасности. Runtime AI можно добавлять **после** границ полномочий и isolation, а не наоборот.

## 3. Этап OS-0 — закрепить boot baseline

Вход: Stage E и Memory Core M1 исходники. Выход: воспроизводимый baseline commit/tag, QEMU/OVMF, BIOS regression, чёткая карта памяти и hardware report. Команды на Ubuntu 24.04: `cd /home/quaxis/IntellectOS && make clean check && make uefi-e-run` (первый инструмент должен быть доступен в текущем репозитории). Не писать на USB автоматически; raw images создаются под `build/`. Проверить состояние Git перед изменением, рабочие ветки, отсутствие build artifacts/private keys. Ни IVX, ни Vibelang к bootloader не приклеивать.

## 4. Этап OS-1 — PMM/VMM hardening

Сделать: устранить глобальное RWX identity-map после безопасного relocation; отдельные kernel/user mappings, physical frame ownership/zeroing, guard pages, W^X включая aliases, COW, invalidation, bounded kernel heap, memory above 4 GiB после правильных mappings, allocator concurrency policy и тесты CPU/page faults. Упор на **исправление фактических M1 ограничений**, а не переименование M1 в M2.

**Gate:** intentional faults only in QEMU сначала; invalid PTE, NX execution, U/S protection, double free, allocated/reclaimed firmware page, overlapping E820/UEFI maps, OOM rollback, guard-page trap, TLB invalidation. Сохранить BootInfo ABI v1.

## 5. Этап OS-2 — ring3, syscalls, scheduler

User entry/return с безопасными register saves и CPU privilege transition, opaque handle table, process/thread lifecycle, minimal scheduler, timer, bounded IPC queues и безопасное copy user memory. Не начинать с GUI или USB stack. **Gate:** два процесса не могут читать/менять память друг друга; закрытые/recycled handles не работают; процесс завершается и освобождает все ресурсов; syscall с invalid user pointer возвращает ошибку, не kernel panic.

## 6. Этап FMT-0 — freeze минимального IVX wire ABI

Параллельно с OS-1/2: согласовать header 128 bytes, segment descriptors 64, directory records 32, signed region, 4-KiБ Merkle leaves, IVXS, image_kind, permissions, import structure, zeroed reserved, valid endian/arch policy. Написать два независимых offline readers и binary fixture generator. **Gate:** malformed corpus rejects, golden byte vectors, same root on independent implementations, file-size/range/overlap fuzz, no ambiguous parse. Формат ещё не загружается в kernel.

## 7. Этап FMT-1 — минимальный `.ivx` loader

Один статический x86-64 PIE executable, без `.ivl`, без lazy linking, без JIT/AI/GUI. Начало user-space: entry, stack, BSS zeroing, RX/RO/RW/NX, signature check, trusted artifact root, output через минимальный разрешённый console syscall, `process_exit`. Для первого запуска можно eager verify весь image, затем page-on-demand после дополнительной проверки. **Gate:** корректный hello выходит, wrong signature/WX/entry/out-of-bounds/missing feature отвергаются, power failure не публикует partially verified image.

## 8. Этап CMP-0 — compiler frontend на host

Lexer/AST/CST из `.vibe`, diagnostic spans, основной type system, parser golden fixtures. Пока можно генерировать только integer/boolean функции и `main` с простым ProcessContext; это не нарушает долгосрочный язык, пока неподдержанное явно отвергается. **Gate:** successful minimal examples + semantic negative corpus, identical build manifests, no nondeterministic paths.

## 9. Этап CMP-1 — VSG/VBIR

VSG semantic graph, type/effect/borrow checker для выбранного поднабора; SSA VBIR with independent verifier и basic optimization passes. **Gate:** verify malformed/duplicate/block-dominance/borrow/effect misuse, deterministic round trip, DCE/constant fold equivalence tests.

## 10. Этап CMP-2 — x86-64 backend и `.ivo` linker

Сначала можно использовать существующие LLVM primitives, но конечный `.ivx` обязан иметь собственную корректную сериализацию. `.ivo`: relocation/test fixtures, link/branch ranges, optional minimal unwind. **Gate:** integer functions под QEMU, compare with reference interpreter/oracle в host tests; build reproducibility; независимый IVX validator.

## 11. Этап RUNTIME-1 — библиотеки и типизированный ABI

Implement `.ivl`, imports/exports, interface/signature hash, eager BIND/RELRO, TLS initial closure, static init DAG, canonical function addresses; Install Manager creates exact `ClosureRecord`. **Gate:** success across two libs, incompatible signature fails before `main`, update side-by-side, stale closure rejects, no mutable text patch.

## 12. Этап SYS-1 — файловые службы, пакеты, ключи

Basic VFS/verified content store/transactional package manager, `.ivpkg` parser with bounded extraction, developer trust, optional system trust chain, lifecycle journal, typed IPC, logging and shell. **Gate:** crash at every install step recovers old or new coherent closure; signed metadata does not automatically confer privileges; an untrusted library cannot write system policy.

## 13. Этап OPT-1 — измеряемые оптимизации

Relative vtables, safe ICF, hot/cold segments, PGO, compact unwind, static TLS fast path, precomputed closure, huge page budget, link-group direct calls as optional advanced optimization. **Gate:** compare FASTMAP/BALANCED/COMPACT by cold/warm p95, branch instructions, PSS/shared RX and page verify overhead; no regressions in integrity/security tests. Бенчмарки воспроизводимы и включают несколько разных workloads, а не один hello world.

## 14. Этап AI-1 — интеллектуальные инструменты

AI service выполняется сначала на Ubuntu host или в изолированном user process, предлагает исходные патчи и профили, а build pipeline пересобирает и верифицирует. При отсутствии модели обычный компилятор работает. **Gate:** prompt injection из README не может вызвать команду OS policy; AI ошибочная трансформация отклоняется verifier/test; исходники не экспортируются во внешний API без разрешения.

## 15. Этап OS-AI-1 — умные функции IntellectOS

Read-only диагностика, объяснение ошибок, представление telemetry, неавторизованные предложения без действий. Позже supervised automation через CapabilityBroker, explicit user grant, receipts и rollback. **Gate:** ядро и `.ivx` продолжают работать при полном выключении AI service; model crash/OOM не влияет на scheduler.

## 16. Этап PORT-1 — другие ISA и платформы

AArch64/64KiБ MMU, CET/BTI/PAC/MTE, RISC-V/CHERI только после отдельного machine ABI. Каждый target — независимый signed image из общего Vibelang/VBIR, выбираемый `.ivpkg` install manager; **универсальный fat `.ivx` не требуется**. Совместимость требует QEMU и реального железа по мере появления, нельзя объявлять CHERI implementation на обычном x86-64.

## 17. Рекомендуемая структура репозитория

```
IntellectOS/
  boot/                     # существующий boot code, без фиктивных IVX изменений
  kernel/                   # существующая foundation + будущие memory/process
  docs/                     # архитектурная документация
    specifications/
      00_README_ARCHITECTURE_MAP.md
      ...
  tools/                    # host build/verifier CLI
  tests/                    # host/QEMU fixtures and fuzz
  (позже) userspace/
    services/               # install manager, VFS, policy, console
    runtime/                # startup shim, library ABI
  (позже) vibelang/
    frontend/
    vsg/
    vbir/
    backend/
    linker/
  (позже) formats/
    ivx/
    ivo/
    ivpkg/
  (позже) intelligence/
    broker/
    planner/
    agents/
```

Это **целевая структура**, не утверждение о существующих каталогах. Не создавать отдельный «debug» репозиторий, не перекладывать настоящие исходники хаотично и не смешивать новые ТЗ с текущими несвязанными торговыми проектами.

## 18. Engineering workflow

Каждая задача: (1) выбрать узкий contract/acceptance; (2) определить существующий baseline и неразрешённые предпосылки; (3) изменить только относящийся модуль; (4) добавить unit/property/negative tests; (5) `make clean check`/соответствующие gates; (6) QEMU first; (7) hardware last; (8) документировать текущие ограничения; (9) commit с понятным scope, без build artifacts/secrets; (10) не объявлять complete, пока нужная проверка не выполнена.

Не требовать от ИИ-компилятора «формально доказать все программы» в первой итерации, но не исключать базовый independent verification ради обещания скорости.

## 19. Политика версий до первой стабильной реализации

Каждый формат/ABI стартует с собственного `major=1, minor=0`, а все предложения до freeze исправляются **внутри проектной v1.0**, без фиктивных миграций 0→1→2. Реальная несовместимая версия появляется только после выпуска стабильного reader/writer и необходимости поддерживать существующие артефакты. BootInfo ABI v1 не меняется. Политика изменения Linux host toolchain — вне runtime ABI.

## 20. Release gates

**Alpha:** boot+process security gates и статический `.ivx` с полной подписью.

**Developer Preview:** Vibelang source → verified `.ivx`, CLI, минимальные типы/права, basic debugger, deterministic builds.

**Beta:** `.ivl`/closure/IPC/VFS/packages, crash recovery, performance comparisons.

**Stable:** independent security audits, fuzz coverage, supported hardware matrix, signed updates/recovery, reproducible release artifacts and public ABI conformance suite.

**AI Preview:** дополнительно только после работоспособной ОС и toolchain; считается опциональной функцией, не критерием безопасности kernel.

## 21. Зависимости и риски

| Риск | Симптом | Митигирование |
|---|---|---|
| Избыточный scope | десятки демонов без работающего `exec` | минимальный signed IVX first |
| Подмена терминов | `.vbin` и `.ivx` смешиваются | один naming registry и проверки |
| Kernel TCB разрастается | сложный parser/LLVM в kernel | offline Install Manager, flat load plan |
| Исчезновение reproducibility | разные корни от одинаковых inputs | pinned toolchain, canonical formats |
| AI делает опасные изменения | компрометация policy/user data | typed proposal+capability broker |
| Исчезновение ASLR/shared pages | personal code patching | immutable TEXT, sealed gates |
| Разногласие ABI | сбои на imports/ownership | hash/type ID, exact closure, test fixtures |
| Power loss during install | смешанные dependencies | journal+atomic generation |
| Переход на новое железо | unsupported ISA/page profile | separate signed images, explicit failure |

## 22. Правило продолжения проекта

Любой следующий диалог/инструмент использует эту схему как основу: **IntellectOS — ОС, Vibelang — исходный язык `.vibe`, ИИ-компилятор пока без имени, его результат — `.ivx` через проверяемый VSG/VBIR/`.ivo`**. Изменения сначала фиксируются как контракт и тест, затем реализуются, затем отмечаются в отчёте фактической проверки. Не заменять договорённость новым названием без явного решения.
