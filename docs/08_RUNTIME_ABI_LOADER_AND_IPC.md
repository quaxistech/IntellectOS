# IntellectOS User ABI, загрузчик IVX и IPC — системный контракт V1

**Статус:** DESIGN DRAFT, не реализовано в текущем Memory Core M1. **Область:** native x86-64 user mode, `.ivx` executable и `.ivl` library; не изменяет 152-байтный BootInfo ABI v1 между firmware loader и ядром.

## 1. Разграничение ABI

- **BootInfo ABI v1** — существующий интерфейс firmware bootloader → native kernel: header 152 bytes, memory records 24 bytes, все addresses физические. Только bootstrap.
- **IntellectOS User ABI v1** — планируемый interface user-process → kernel и runtime services: syscall calling convention, process entry, registers, handles, memory regions, IPC, errors, feature negotiation.
- **Vibelang Public ABI v1** — language-level typed interfaces/import/export с SignatureId, ownership, effect declarations, layout mode; не является набором сырых syscalls.
- **IVX File Format v1** — байтовое описание подписанного native image; loader валидирует его до выполнения.
- **Internal LoadPlanV1** — проверенная установщиком flat representation для исполнения; не пользовательский file ABI и не исполняемый код.

Эти пять контрактов версионируются отдельно. Совпадение `v1` в именах не означает идентичные структуры, flags или сроки реализации.

## 2. Типы и соглашения

Первый target — x86-64 little-endian, kernel/user pointer width 64. Любой ABI-size определён отдельно от C host `sizeof()`; wire integers little-endian и fixed-width. Системный syscall может использовать архитектурный fast entry (`syscall/sysret` при доказанно безопасных переходах или `iretq` fallback) — точные регистры/номера необходимо утвердить до запуска реальной программы. В текущем документе имена syscalls проектные, но **не** выдуманные production IDs.

### 2.1. Привилегии

Kernel исполняется в ring0; обычное `.ivx` — ring3. User memory всегда с U/S checks, kernel stacks/private page tables недоступны из user, NX/W^X enforced и CR0.WP/NX verified. Переключение контекста очищает/сохраняет sensitive CPU state по platform security policy. ИИ-код не работает в kernel mode для обслуживания syscalls.

### 2.2. User entry contract

При `spawn` runtime создаёт процесс, private stack c guard pages, TLS, `ProcessContext`/capabilities и entry trampoline. Kernel передаёт **только утверждённые launch descriptors**: ABI version, argc/argv contract, selected handles, stack layout, TLS pointer, environment data allowlist, closure generation, feature bitmap. Старт `main` происходит через доверенный runtime shim после RELRO/sealing и разрешённого init DAG. Без библиотек возможен минимальный `entry` с прямой syscall exit, но никакого произвольного constructor перед подготовкой memory protection.

**Entry stack**: 16-byte alignment там, где требует ISA ABI; страницы stack NX; размер/рост ограничен process policy; записи argv/env в приватных read-only или user buffers без передачи необработанных kernel pointers.

## 3. Kernel handles

`HandleValue` — opaque process-local integer/reference (предлагаем 64-bit opaque ID). В ядре `HandleTableEntry{object_id,generation,rights,object_kind,refcount,revocation_domain}`. Обязательна проверка `(handle ID, generation, owner)`; recycling без поколения создаёт use-after-close. `duplicate` может только уменьшать права. Передача handle в IPC требует send-rights на объект и explicit transfer rights; владелец/получатель и состояние escrow фиксируются атомарно. Никакой filename не служит автоматическим токеном полномочий.

Rights примеры: `READ`, `WRITE`, `EXECUTE` (только verified mapping), `DUPLICATE`, `TRANSFER`, `MAP`, `WAIT`, `SIGNAL`, `MANAGE`. Специальные эффекты `fs.read`, `net.connect`, `console.write` проверяются в user service и kernel capability APIs; это не глобальный бит «всё читать».

## 4. Syscall groups — планируемые функции

| Группа | Операции | Проверка |
|---|---|---|
| handles | close, duplicate_limited, info | generation, rights |
| process | spawn_verified, wait, exit, info_limited | executable closure, ownership |
| thread | create, join, exit, set_tls | address space, stack bounds |
| VM | map_verified, map_anonymous, unmap, protect_limited | W^X, reservation, page owner |
| IPC | channel_create, send, recv, wait_many | length/handles/backpressure |
| time | monotonic_read, timer_arm, timer_cancel | no wall-clock trust for security |
| log | write_scoped | rate/size limit, privacy |
| device | open_capability, submit_io | DMA/IO rights, isolation |

Запрет: передача naked physical address в syscall обычного приложения; `mprotect` изменение writable user page на RX без JIT authorization/verifier; global `open` arbitrary path без scoped file system namespace. Ошибки имеют typed categories `BAD_HANDLE`, `DENIED`, `INVALID_ARGUMENT`, `OUT_OF_RANGE`, `NOT_SUPPORTED`, `NO_MEMORY`, `STALE_GENERATION`, `INTEGRITY_FAILURE`, `VERSION_MISMATCH`, `BUSY`, `TIMEOUT`, `CANCELLED`.

## 5. IPC wire profile

Предлагаемый `ChannelMessageHeaderV1` — 32 байта little-endian; это **проект с полностью подсчитанной базовой таблицей**:

| Offset | Bytes | Field | Purpose |
|---:|---:|---|---|
| 0 | 4 | `magic` | `0x31435049` = ASCII `IPC1` in LE |
| 4 | 2 | `major` | 1 |
| 6 | 2 | `minor` | 0 |
| 8 | 4 | `interface_index` | per-channel installed interface ID |
| 12 | 4 | `method_id` | canonical method ordinal |
| 16 | 4 | `payload_bytes` | bounded byte length |
| 20 | 2 | `handle_count` | bounded handle transfers |
| 22 | 2 | `flags` | request/response/cancel known bits |
| 24 | 8 | `correlation_id` | caller-assigned opaque nonce/id |
| 32 | | **end** | |

Handle payloads передаются отдельно от serialized bytes через kernel transfer list, чтобы нельзя было подделать capability простым повторением integer. Message body — canonical typed schema, не универсальная рекурсивная JSON-интерпретация в ядре; version negotiation происходит до high-speed IPC, unknown required fields вызывают reject. Для больших сообщений передавать shared-memory handles с explicit rights, не копировать мегабайты через kernel header.

## 6. Internal Install Closure

`ClosureRecord{generation, root_image_digest, ordered_artifact_digests[], resolved_imports[], ABI_verdict, static_TLS_layout, init_DAG, trust_policy_id, capability_request_digest, resource_policy}` создаётся доверенным Install Manager. `artifact_digests[]` включают exact content root `.ivx/.ivl`, позволяя side-by-side версии без циклических зависимостей внутри самих подписанных файлов.

`resolved_imports` хранит `(consumer_artifact_index, import_index, producer_artifact_index, producer_rva, signature_id)`; **не абсолютный virtual address**, иначе ASLR и воспроизводимость ломаются. Изменённый lib content root инвалидирует closure, но уже работающий старый процесс может завершаться на предыдущем поколении.

## 7. LoadPlanV1 — минимальные доверенные таблицы

Валидатор после полной offline проверки создаёт:

- sorted mapped regions с file handle и `[file_off, len)`;
- segment type, `virtual_rva`, final permissions и page profile;
- sorted per-page fixup recipes, bounded count и patch cell ownership;
- import index → exact library digest/RVA;
- precomputed TLS template and layout offsets;
- init ordering, strict entrypoint, closure/generation;
- verified root, signature policy verdict, minimum OS/kernel feature set.

Kernel принимает LoadPlan **только** из проверенного system service/protected store и повторно проверяет digest/generation/ranges/permissions против trust anchor. Простая передача `*.ivplan` из home directory не должна позволять обойти verification. Формат внутреннего плана версия-специфичен и не приравнивается к portable IVX format.

## 8. Старт процесса — последовательность

1. `spawn_verified` получает scoped VerifiedArtifactHandle, process principal и policy.
2. Проверяет canonical IVX header, `image_kind=EXEC`, allowed ISA/features/ABI, trusted closure generation.
3. Определяет required dependencies и статический TLS по выбранному closure; проверяет, что все exact digests присутствуют и неизменяемы.
4. Резервирует ASLR layout, entry, stack, guards, control and TLS pages. Проверяет branch reachability/gate placement.
5. Располагает shared immutable RX/RO pages без per-process текстовых fixups; если их ещё нет в page cache, проверяет криптографическое дерево по page-in.
6. Готовит private DATA/BIND/TLS, применяет только whitelist fixups ровно один раз на staging копиях.
7. Запечатывает BIND как R, все regions на W^X, TLS init, syscall entry, CFI/hardware policy.
8. Публикует process state READY, передаёт управление trusted runtime shim, вызывает init DAG и затем Vibelang `main`.
9. В случае ошибки уничтожает все staging pages/handles; неполный процесс не запускается.

Нет строкового symbol lookup на обычном exec при наличии готового closure. Нет пользовательских IFUNC resolver/LD_PRELOAD. Повторное открытие ошибочного private page не должно второй раз прибавлять ASLR slide.

## 9. Direct calls, gates и function identity

Local intra-image `call rel32`/`bl`; link-group может иметь стабильные offsets при одном групповом ASLR slide, но это **не** обязательный механизм. Независимые libraries вызываются через eager sealed gate/slot. Gate архитектурно специфичен: AArch64 short `b` 4B с ограниченным радиусом, x86-64 `jmp rel32` 5B и дальний variant с indirect read sealed pointer. Canonical address-of-function `&f` указывает на настоящую entrypoint, не на gate, иначе функция меняет identity.

## 10. TLS

Static TLS планируется для точного initial closure: выравнивание layout и fixed-offset TP access, когда target ABI допускает. Позднее присоединённый plugin не изменяет compile-time fixed offsets; dynamic TLS использует explicit validated descriptor. `ThreadLocal` init/destructor порядок и ошибки определяются runtimes, память каждого потока приватна.

## 11. IPC и компоненты

Нативная `.ivl` внутри одного address space разделяет память и общий fault domain; она не sandbox. Изолированный component живёт в отдельном процессе/защищённом домене и общается только через typed IPC; zero-copy shared memory требует explicit kernel capability и protocol ownership. Для plugin crash изолированный процесс перезапускается; process-local pointers и handles из старой генерации недействительны.

## 12. Capability invocation

`requested capabilities` из манифеста IVX пересекаются с publisher policy, user/admin grants, container/namespace limits и active revocations. Результат — конкретные handles/rights, выдаваемые на startup и при отдельном privilege request с авторизацией. Приложение может запросить net.connect, но без выданного сетевого handle запрос останется отклонённым. ИИ-предложение не считается grant.

## 13. Faults и наблюдаемость

Реестр ошибок loader включает `BAD_MAGIC`, `BAD_IMAGE_KIND`, `UNSUPPORTED_ISA`, `ABI_MISMATCH`, `UNTRUSTED_SIGNATURE`, `STALE_CLOSURE`, `MISSING_DEPENDENCY`, `W_X`, `BAD_FIXUP`, `PAGE_INTEGRITY`, `INIT_CYCLE`, `ASLR_PLACEMENT`, `RESOURCE_LIMIT`. Диагностическая запись содержит process id, closure generation, image root, typed error, а не все private paths/bytes памяти. При integrity failure исполняемая страница не публикуется; явная runtime exception завершается безопасным отказом процесса.

## 14. Проверки завершения V1

- UEFI M1/BootInfo regression tests сохраняются без изменений.
- Два ring3 процесса не могут писать/читать чужую память.
- User stack не исполняется, W+X rejected с проверкой алиасов.
- Передача IPC-handle с правами выше исходных отклоняется.
- Подмена exact dependency digest отвергается, старый процесс сохраняет старый closure.
- Обрыв загрузки на каждом из промежуточных шагов не оставляет partially runnable process.
- Entry readonly RX, BIND sealed, private DATA RW/NX.
- Формат `.vsg`/`.vbir`/`.ivo`/`.ivpkg` не принимается как `.ivx`.
- AI-сервисы могут быть выключены без влияния на syscall/loader.

## 15. Невыбранные параметры

Syscall ID map, точный register ABI entry/exit, process context byte structure, kernel IPC queue implementation, handle width, scheduler fairness constants, full `LoadPlanV1` wire layout и naming service остаются подлежащими freeze до реализации. Пустоты обозначены явно, а не заполняются фиктивными номерами, которые затем случайно станут зависимостью проекта.
