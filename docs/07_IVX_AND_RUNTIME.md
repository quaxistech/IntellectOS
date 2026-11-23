# IVX-1, загрузчик, пользовательский ABI и runtime

**Подробная исходная спецификация:** `specs/05_IVX_BINARY_EXECUTABLE_SPEC.md` — сохранена без сокращения. Ниже единый системный контракт и граница реализованного профиля.

## 1. Типы образов

| Артефакт | Роль | Исполнение |
|---|---|---|
| `.vibe` | исходник Vibelang UTF-8 | не исполняется ядром |
| `.vsg`, `.vbir` | семантический граф и typed SSA | не исполняются ядром |
| `.ivo` | relocatable native object | не допускается `exec` |
| `.ivx` | verified native executable, image_kind EXEC | только после loader, trust и policy |
| `.ivl` | verified shared image, image_kind SHARED | библиотека, не entry process |
| `.ivpkg` | подписываемый install package/closure | не прямой исполняемый файл |
| `.ivdbg` | detached debug/symbol/source maps | не исполняется |

Не создавать конкурирующие `.vbin`, `.vlib`, `.vobj`, `.vdbg`, `.vpkg` имена. Extension не доказывает format: magic, version, image_kind, signed manifest и exact policy решают.

## 2. Три плоскости IVX

**Immutable file content:** canonical little-endian binary tables, segments with `u64 file_size/mem_size`, memory protection, BLAKE3 Merkle (16-ary tree), detached signature envelope `IVXS` (Ed25519 initial, ML-DSA future policy). **Verified closure:** installed dependency graph, ABI/version/SignatureId, package generations, rights, origin trust. **Private runtime mapping:** ASLR, immutable RX/RO sharing, TLS, relocs outside sealed TEXT, guard pages, typed calls/PLT/gates, init DAG and unwind. Компоновщик может оптимизировать FASTMAP/BALANCED/COMPACT, но только по benchmark и при сохранении исходной идентичности функций.

## 3. Известный базовый бинарный профиль

`IVXHeaderV1` **128 bytes**, `IVXSegmentV1` **64 bytes**, `IVXDirectoryV1` **32 bytes**, Merkle BLAKE3, `IVXS` signature envelope с header **32 bytes** и record **24 bytes** по спецификации. Публикация этих размеров не означает фиксацию всех nested manifest/import/export/fixup/unwind IDs: реестр открытых freeze-блокеров — 19.

Подпись говорит о происхождении и целостности, но не предоставляет root/driver/network права. Kernel/loader получают права только от policy и capability broker.

## 4. Стартовый developer loader и целевой normal loader

Текущий `ifs_exec64.c` принимает один заранее сгенерированный developer-профиль terminal, полностью привязанный к BLAKE3 hash в build-time kernel pin; Ed25519 проверяется host tool до формирования pin. Это **не** general-purpose in-kernel Ed25519 trust store, не multi-app loader, не динамическая `.ivl` linking. Нельзя обманывать тесты словом `IVX verified` без уточнения профиля.

Целевой Verified Install Manager: parse bounded; structural validation; byte/order/range/overflow; strict signature + authorized keys; manifest/executable kind; compatible ISA/ABI; effects/capabilities; import/export hashes; exact dependency closure; verified fixup plan; transaction install and signed release pointer. Затем minimal kernel loader проверяет доверенный `LoadPlan` и устанавливает RX/RO/RW+NX maps в private CR3. Нельзя изменять signed TEXT per process.

## 5. User ABI и process entry

BootInfo ABI 152B ≠ User ABI ≠ Vibelang Public ABI ≠ IVX File ABI ≠ LoadPlan internal ABI. User ABI должен задавать x86-64 calling convention, stack 16-byte alignment, guarded argv/env, thread TLS, syscall/error protocol, 64-bit handles (type+generation+rights), process lifecycle, no direct kernel pointers. Пользовательская программа не получает IO privilege и не может самостоятельно прикреплять DMA device.

Состояние U1: `int 0x80` с `write/read/clear/info/exit`, один CPL3 процесс, frame protection. Следующий gate — concurrent two-process isolation, validated `spawn_verified`, manager/worker relation, IPC, process reap и W^X across aliases.

## 6. Импорт, fixes, interop

Imports имеют exact `SignatureId`, interface/package IDs, ABI major/minor, rights upper bound; exports не могут стать «совместимыми» совпадением укороченного хеша. RELR/CHAIN/CALL_GATE должны назначать каждой patch-cell одного owner, быть bounded, без overlap/cycles, не писать в immutable TEXT. `FUNCTION_FRAGMENTS`, compact unwind и canonical TLS важно сохранять для PGO и исключений; не упрощать всё до одного интервала функции. `.ivl` в общем address space не sandbox; недоверенный plugin — изолированный process/typed IPC.

## 7. Безопасность и тесты

1. Mutated byte in signed region → reject; valid signature with wrong permission scope → reject.
2. Wrong magic/kind/version/ISA/ABI, overflow file offsets, overlapping segment protections, unknown mandatory extension → reject.
3. Null/weak token, stale closure, changed dependency generation → reject.
4. Multiple processes: no kernel access, no W+X, stack NX, no double initialization, no private code patch.
5. Fuzz parser + differential readers + golden bytes + supply-chain/rollback tests.
6. Loader rejects VSG/VBIR/IVO/IVPKG/IVL as `.ivx` process entry.

Файл может быть cryptographically valid, но небезопасным в runtime. Security acceptance отдельно.
