# IntellectOS — системная архитектура интеллектуальной ОС

**Версия проектного документа:** 1.0 · **Дата:** 2026-11-20 · **Статус:** DESIGN DRAFT. Действующие BIOS/UEFI/Memory Core отмечены отдельно; все остальные механизмы являются предлагаемой архитектурой.

## 1. Миссия и функциональные свойства

IntellectOS — самостоятельная операционная система, способная работать без Linux/Windows в роли ядра или скрытой среды исполнения. Она управляет процессорами, физической и виртуальной памятью, таймерами, устройствами, хранением данных, процессами, межпроцессными сообщениями, пользовательской средой и интеллектуальными службами. Язык Vibelang и ИИ-компилятор — **инструменты разработки для IntellectOS**, а `.ivx` — формат готовых приложений. Важно не смешивать интеллектуальность платформы с исполнением каждой машинной инструкции: криптография, MMU, прерывания, права доступа и планирование обязаны корректно работать без модели.

**Критерии качества:** собственный детерминированный TCB; небольшие привилегированные компоненты; низкая латентность запуска; прозрачные ресурсы; независимые rollback-обновления; воспроизводимость; аппаратная переносимость по портам; явно контролируемые права; возможность ручной работы при отказе AI; собственные стандарты интерфейсов без копирования наследуемых ошибок существующих ОС.

## 2. Исполняемые домены и слои

```
Hardware / firmware: CPU, MMU, APIC, UEFI/BIOS, ACPI, PCI/PCIe, storage, NIC, GPU
   │
   ├─ Boot Layer: BIOS+UEFI → BootInfo ABI v1 → native entry
   ▼
Kernel / privileged (детерминированно):
   PMM, VMM, scheduler, interrupts, time, handles, syscall, IPC,
   page cache/verifier, process/thread lifecycle, minimal driver primitives
   │
   ├─ Trusted system services (user mode, isolated):
   │   driver manager, VFS/file server, mount manager, security policy,
   │   package/install manager, IVX resolver/link planner, logging, key service
   │
   ├─ Optional AI services (user mode, revocable capabilities):
   │   assistant, planner, diagnostics, performance adviser, indexing,
   │   local inference, device-automation broker
   │
   └─ Applications and tools:
       shell, developer tools, file manager, GUI, Vibelang compiler, IVX apps
```

**Предлагаемый стиль:** минимально привилегированное ядро плюс вынесенные системные службы, но не догматически «всё только microkernel». Сохранение производительности, аварийного восстановления и проверяемости определяет размещение компонентов. Драйвер, реально требующий kernel mode, может работать там после аудита; стандартная цель — обособление драйверов с минимальными доступами.

## 3. Что существует сейчас

Действующий базис: `boot/bios`, `boot/uefi`, `kernel/i386`, `kernel/x86_64`, `kernel/src64`, `kernel/include`, `docs`, `tests` и `tools`. Legacy BIOS-загрузка использует MBR/Stage1 и 32-битный protected mode; UEFI-x86_64 загружает собственное ядро через `BOOTX64.EFI` и `ExitBootServices`. Эти два kernel binary **разные**. `BootInfo v1` состоит из 152-байтного заголовка и массива 24-байтных записей. На 2026-11-19 Memory Core M1 имел отчёт 31 успешного теста, работающий ограниченный PMM и high-half supervisor-only RW/NX VMM на QEMU/OVMF и Lenovo. Существующий низкий identity mapping всё ещё RWX; нет kernel heap, user mode, полноценного scheduler и приложений. Планы ниже не являются описанием уже реализованного кода.

## 4. Загрузка и граница доверия

### 4.1. Legacy BIOS

`BIOS → custom MBR → Stage1 → E820/BootInfo → i386 kernel`. До получения 64-bit общего ядра эту ветку сохраняем как независимый совместимый режим диагностики. Не переносим новые структуры IVX в BIOS MBR. Для 64-битной пользовательской платформы необходимо отдельно определять возможность перехода из Legacy Boot в единый x86-64 kernel, а не считать его имеющимся.

### 4.2. UEFI x86-64

`UEFI firmware → BOOTX64.EFI → GOP/ACPI/SMBIOS → memory map → ExitBootServices → native kernel`. После `ExitBootServices` ядро не вызывает Boot Services. Firmware Runtime Services допустимы лишь после отдельного контракта virtual mappings и ownership. Loader-owned страницы не возвращаются PMM на основании одного `type=usable`.

### 4.3. BootInfo ABI v1 — не IVX ABI

BootInfo: magic `0x49534F33`, version=1, header=152, карту памяти хранит отдельно; поля содержат физические, не виртуальные адреса. Типы памяти: usable/reserved/ACPI reclaimable/ACPI NVS/bad. У новых компонентов есть **свой** ABI. Не менять offsets BootInfo и не вводить `IVXHeader` в firmware handoff. Точность и lifetime BootInfo проверяются отдельно, прежде чем читать ACPI/SMBIOS.

### 4.4. Цепочка безопасного запуска

`firmware identity → bootloader/kernel identity → trusted root policy → trusted services → verified application closure`. На раннем этапе допустим developer trust store, но он не равнозначен production Secure Boot. Даже подписанная программа при запуске получает только разрешённые ОС полномочия.

## 5. Режимы CPU и память

### 5.1. Физический аллокатор (PMM)

Обязательные проверки: нормализация и пересечения firmware ranges; приоритет reserved над usable; запрет частично занятых страниц; страницы framebuffer, MMIO, runtime, ACPI, стека, page tables, kernel image и BootInfo зарезервированы до доказанного reclaim. Объект `FrameId` имеет физический адрес, размер/порядок и owner generation; освобождение дважды и освобождение отображённого frame запрещены. В будущем поддержать RAM выше 4 GiB только после валидированной карты/отображения.

### 5.2. Виртуальная память (VMM)

Переход от экспериментального high-half scratch window к изолированным address spaces. Обязательные: U/S бит, NX, W^X включая alias, guard pages, ASLR, корректное zero-on-allocate, COW, bounded kernel heap, TLB shootdown при SMP, MMIO policies и контроль page-table ownership. Драйверы не могут читать «всё физическое RAM» по одному необоснованному широкому mapping.

### 5.3. AddressSpace и mapping

`AddressSpace{id,generation,root_page_table,ASLR_state,policy}`; `VmRegion{base,len,protection,backing_id,offset,sharing_domain,owner}`. Диапазоны выравниваются по текущему `PageSize`, не по магическому постоянному 4096; IVX хеш-блок может быть независимо 4 КиБ. Проверка mapping атомарна относительно faults: нельзя увидеть частично расставленные PTE для неподписанного RX.

### 5.4. Fault handling

Fault: классифицировать `not_present`, `protection`, `copy_on_write`, `verified_file_page`, `stack_guard`; определить владельца mapping; перед разрешением RX подтвердить доверенный digest. Нельзя вызывать ИИ, произвольный парсер пакета или потенциально рекурсивно лочащий IO на атомарном пути исключения. Все состояния страницы `ABSENT→STAGING→VERIFIED→PUBLISHED` либо `FAILED`, повторный вызов идемпотентен.

## 6. Процессы и потоки

`Process{id,generation,address_space,closure_id,security_context,handle_table,threads,parent,resource_budget}`; `Thread{id,process_id,registers,stack,user_TP,kernel_stack,sched_state}`. Состояния процесса: `CREATING→VERIFIED→READY→RUNNING⇄WAITING→EXITING→ZOMBIE/REAPED`; недопустимые переходы отвергаются. Разделять id и generation для защиты от устаревших ссылок. Завершение процесса отзывает его handles и освобождает память после подтверждения отсутствия ссылок от threads/kernel.

Переключение контекста: архитектурно определённые регистры, FPU/SIMD save policy, kernel preemption policy, user/kernel transition ABI, FPU leakage restrictions, RCU/lock primitives для будущего SMP. Нет предположения, что код компилятора может обойти защищённый syscall boundary.

## 7. Планировщик

**MVP:** single-core runnable queue с bounded fairness; приоритеты и yield/block/wakeup; отсутствие busy wait; таймер и монотонное время. **Следующий этап:** preemptive SMP с per-CPU queues, affinity, NUMA hints, latency classes и CPU quotas. Интеллектуальный советник может рекомендовать параметры, но atomic scheduler decisions остаются алгоритмическими и проверяемыми; деградация модели не должна блокировать работу ПК.

Возможные классы: `INTERACTIVE`, `NORMAL`, `BATCH`, `LOW_LATENCY`, `BACKGROUND`; политики starvation-prevention и max-priority elevation; статистика p50/p95/p99 scheduling latency без изменения безопасности. Параметры приоритетов и лимитов выдаются системной политикой, не описанием приложения.

## 8. Системные вызовы и IPC

Минимальный набор primitives: `handle_close`, `handle_duplicate_limited`, `channel_create`, `channel_send`, `channel_recv`, `wait`, `thread_create`, `thread_exit`, `process_exit`, `vm_map`, `vm_unmap`, `vm_protect_limited`, `clock_read`, `log_write_limited`. **Это проектные имена, не уже закреплённые номера syscalls.** У каждого вызова фиксированная архитектурная calling convention, проверка user pointers, length, alignment, lifetimes, cancellation и error codes. Никакие process-global FD номера не являются сами по себе правом; только проверяемый kernel handle с generation и rights.

IPC: типизированные сообщения на интерфейсном уровне, низкоуровневая передача байтов + конкретных capabilities на ядровом уровне; versioned message header и bounded payload. Передача handle может только сужать права. Shared memory — явно созданный объект с отражёнными read/write/execute ограничениями, квотами и протоколом согласованности. Cross-process ABI не должен слепо передавать необработанные C pointers.

## 9. Файловая подсистема

Kernel VFS primitive → изолированные file-system services → block-device drivers. Типы: `FileHandle`, `DirectoryHandle`, `MountHandle`, `VerifiedArtifactHandle`, `TransactionHandle`. Доступ — через выданный каталог/handle, не через строковый полный путь в обход namespace политики. Разделение namespaces процессов и приложений; atomic replace/rename, durable flush, crash-consistent package activation. Файловая система для boot image не равна будущей основной FS: текущие 32-MiB raw images не демонстрируют наличие общего FAT-драйвера ядра.

### 9.1. Хранилище установленных IVX

Content-addressed immutable blobs с `content_root` и refcounts; transaction log для публикации closure поколений; state `STAGED→VALIDATED→ACTIVE` или `REJECTED`; garbage collection только после отсутствия holders старого поколения. `LoadPlan` создаётся доверенным сервисом после полной проверки, а не загружается из `$HOME/app.ivplan`.

## 10. Драйверы и аппаратура

Этапы: `PCI/ACPI/SMBIOS discovery` → memory/IOMMU topology → BAR/MMIO mapping → interrupts/MSI → DMA validation → device-specific protocol → IO tests. Перечень устройств не означает, что существуют драйверы. Базовая стратегия: serial console, framebuffer, monotonic timer, keyboard/input, storage, network, USB, audio, GPU/accelerator поэтапно. DMA требует IOMMU или другой доказанной модели изоляции; память пользователя не выдаётся устройству без pin и bounded lifetime. PCI конфигурацию читают и изменяют только специализированные службы/ядро с необходимым правом.

Сбои драйвера должны изолироваться и перезапускаться, если устройство и DMA ownership позволяют. AI-служба может анализировать телеметрию и рекомендовать настройки, но не записывает MMIO сама.

## 11. Служба загрузки приложений

`.ivx` является готовым нативным образом для одной ISA; `.ivl` — библиотекой. Install Manager проверяет подписи, ABI, типы, граф зависимостей, права и формирует проверенный `Closure Record` и `LoadPlanV1`. На `spawn` kernel loader получает только доверенные дескрипторы и плоскую metadata, выделяет ASLR, создаёт sealed BIND, TLS и stack, проверяет RX/RO страницы и входит в пользовательский entry shim. Контракт подробно расписан в спецификациях IVX и ABI.

## 12. Службы безопасности

`PolicyEngine` (не в AI), `TrustStore`, `PackageManager`, `AuditLog`, `CapabilityBroker`, `SecretStore`, `KeyService`. Root rights на изменение политик не получает обычная программа. Root системного доверия не должен быть тождественен ключу разработчика. Нужны безопасные recovery/rollback пути с измеряемым состоянием. При отказе служб политика **fail-closed для повышения привилегий**, но не обязательно прекращает все уже разрешённые вычисления.

## 13. Умные подсистемы ОС

Отдельные процессы: пользовательский ассистент, индексатор файлов, советник энергопотребления, анализатор падений, разработческий агент и локальный inference service. Для любой операции модель выдаёт `proposal`, а deterministic executor применяет её через права, schema validation и подтверждения. Не хранить ключи в prompt и не доверять тексту документов как командам системы. Подробнее — `02_INTELLECTOS_INTELLIGENCE_LAYER.md`.

## 14. Интерфейс пользователя

Первым должен быть текстовый shell/CLI и diagnostics без GPU. Затем compositor, input routing, оконная система, rendering API, accessibility, международные раскладки, выбор локализации, secure prompt для операций с привилегиями. UI не является частью ядра и должен перезапускаться без разрушения процессов. Голосовые и AI-интерфейсы — опциональные клиенты с теми же правами, что у GUI.

## 15. Метрики, диагностика и операционная поддержка

При boot: BootInfo validation, reserved frames, VM mappings, таймер, ядровые паники. При runtime: task counts, scheduler wait, page faults/second, per-process PSS/RSS/private dirty, verified page cache, syscall latency, IPC queue depth, IO error categories, срабатывания policies. Не включать в логи без необходимости содержимое пользовательских сообщений/секреты. Crash records подписываются/маркируются контекстом сборки, процессом и IVX content root.

## 16. План проверки модулей

| Фаза | Входной gate | Проверяемый результат |
|---|---|---|
| Boot | Stage E + M1 | BootInfo v1 без регрессий, NX scratch проверен |
| Memory hardening | M1 | полное W^X, guard pages, защита low mapping |
| User mode | Memory hardening | кольцо 3, syscall traps, изоляция двух процессов |
| Files/IPC | User mode | handles с generation, права на передачу, FIFO/queue safety |
| IVX loader | User mode + trusted store | signed статический `.ivx` запускается и завершается |
| Library closure | IVX loader | `.ivl` импорт, ABI hash, exact generations, RELRO |
| Developer tools | user space или host cross-build | `.vibe` → `.ivx` без модели на пути запуска |
| AI layer | проверки sandbox/capability | model outage = CLI/ядро/процессы продолжают работать |

## 17. Необязательные направления и их цена

Hypervisor, POSIX layer, AArch64, RISCV64, WebAssembly compatibility, GPU compute, distributed storage, live patch, JIT и адаптивный scheduler — будущие задачи, **не** автоматические требования к минимальному ядру. Каждое расширение обязано доказать пользу и не расширять привилегированную поверхность до проверки.

## 18. Инварианты приёмки

- Ни одна карта RAM не принимается без bounds/ownership проверок.
- Ни одна программа не запускается по одному расширению `.ivx` или наличию заголовка.
- Ни один user ptr не разыменовывается ядром без проверки и копирования по безопасной стратегии.
- Подписанные RX страницы не изменяются per-process при обычном запуске.
- Корректность IVX не зависит от связи с сетью/ИИ.
- Устройство не получает доступ к физическим кадрам вне выданного DMA mapping.
- При сбое install/обновления старый active closure остаётся целым.

**Источники конкретного состояния:** текущий публичный `README.md`, `docs/ARCHITECTURE.md`, `docs/BOOTINFO_V1.md`, `docs/MEMORY_CORE_M1.md`, `docs/MEMORY_CORE_M1_VERIFICATION.md` репозитория `quaxistech/IntellectOS`. Остальное — проектные архитектурные требования данного комплекта.
