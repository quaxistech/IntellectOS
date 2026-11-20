# Семейство файлов Vibelang и IntellectOS: .vibe, .ivo, .ivl, .ivpkg, .ivdbg и проектные метаданные

**Редакция:** 1.0 DRAFT · **Статус:** проектный форматный реестр; стабильное согласованное расширение программы — `.ivx`. Ни один из перечисленных файлов, кроме `.ivx`, не является самостоятельной точкой `exec` пользовательского процесса.

## 1. Реестр

| Артефакт | Типичный путь | Признак | Владелец/инструмент |
|---|---|---|---|
| `.vibe` | `src/main.vibe` | UTF-8 исходник | developer / Vibelang frontend |
| `vibe.project.toml` | в корне проекта | декларативный build manifest | build manager |
| `vibe.lock` | в корне проекта | точные digests dependencies | resolver/build manager |
| `.vsg` | `build/ir/main.vsg` | typed semantic graph | semantic frontend |
| `.vbir` | `build/ir/main.vbir` | typed SSA | optimizer/backend |
| `.ivo` | `build/obj/main.ivo` | relocatable machine code | assembler/linker |
| `.ivx` | `build/bin/app.ivx` | подписанный executable IVX-1 | OS Install Manager/Loader |
| `.ivl` | `build/lib/std_console.ivl` | IVX-1 SHARED image | OS Install Manager/Loader |
| `.ivpkg` | `dist/app.ivpkg` | версия и ISA-артефакты/ресурсы | package installer |
| `.ivdbg` | `build/debug/app.ivdbg` | отладочные данные и source map | debugger/symbolizer |

**Нет `.vbin` как альтернативы `.ivx`.** Старые имена `.vobj/.vlib/.vdbg/.vpkg` не применяются в новом naming contract. Файл с расширением `.ivx`, не соответствующий magic и проверенной подписи, отвергается. `vibe.lock` и package manifest — **разные** сущности: первый фиксирует зависимости сборки, второй описывает публикацию/установку.

## 2. `.vibe` — исходный файл

Кодировка UTF-8 без BOM; LF preferred; CRLF при чтении допустим лишь с корректным source-span mapping. Семантика определяется грамматикой Vibelang; не может зависеть от файлового имени, часов сборки или AI-модели. Каждая единица компиляции начинается с `module path;`, список `use` разрешается строго по проекту. Non-NFC string payload не меняется автоматически без семантического правила; имена экспортов в базовом языковом V1 — ASCII identifiers.

## 3. `vibe.project.toml` — project manifest

Предлагаемый TOML-профиль с запрещением неизвестных обязательных ключей и явными build profile полями. Пример **проектного** синтаксиса:

```toml
format = 1
language = "1.0"

[package]
name = "demo.hello"
version = "0.1.0"
kind = "app"
root = "src/main.vibe"

[target]
triple = "x86_64-intellectos"
abi_major = 1
page_profile = "4k"
required_features = []

[build]
profile = "balanced"
reproducible = true
lto = "thin"

[capabilities]
request = ["console.write"]

[dependencies]
"std.console" = "^1.0"
```

`package.name` — логическая identity, не UNIX path. SemVer range требует lock resolution; при сборке в offline mode транзитивные точные версии берутся из lockfile и content store. `request` — требуемые capabilities приложения; они не предоставляются компилятором. `build.profile` выбирает формирование изображения FASTMAP/BALANCED/COMPACT, не меняет архитектуру ABI. Сортировка исходных paths нормализуется; файл не должен содержать неограниченные script hooks, которые запускаются автоматически с правами пользователя.

## 4. `vibe.lock` — воспроизводимый граф сборки

В v1 предлагается каноническая текстовая форма с фиксированным порядком и строгим parser; каждый узел содержит `logical_package`, version, source registry identity, полный `source_digest`, dependencies sorted by full identity, selected feature set, stdlib digest. Digest включает нормализованную каноническую сериализацию lock content, но **сам digest lock не записывается рекурсивно внутрь lock как собственное обязательное поле**.

Фрагмент:

```toml
format = 1

[[package]]
name = "std.console"
version = "1.0.0"
source = "intellect:standard-library"
digest = "blake3:FULL_64_HEX_CHAR_PLACEHOLDER"
features = []
```

Здесь `FULL_64_HEX_CHAR_PLACEHOLDER` — **иллюстративный плейсхолдер**, не допустимое валидное значение. При реальной реализации full 32-byte digest кодируется ровно 64 lower-case hex символами после префикса. Повторяющиеся package IDs с конфликтующими digests — ошибка. Unknown mandatory keys — fail, в v1 нельзя иметь пакет с неразрешёнными зависимостями. Публичная версия и content identity — не одно и то же.

## 5. `.ivo` — relocatable object

### 5.1. Состав

Файл создаётся после VBIR lowering, но не запускается OS loader. Содержит одну ISA/ABI, таблицу атомарных секций/блоков, машинные байты, IDs функций/символов, compact delta relocations, `.addrsig` аналог, source/provenance references, optional block layout map, compact unwind build metadata и optional ThinLTO summary. Вся семантика исходников уже проверена; однако linker всё равно рассматривает `.ivo` как недоверенный input.

### 5.2. Заголовок `IVOHeaderV1`: 64 байта, little-endian (проектный wire layout)

| Offset | Bytes | Field | Правило |
|---:|---:|---|---|
| 0 | 8 | `magic[8]` | `49 56 4F 31 00 0D 0A 1A` (`IVO1\0\r\n\x1a`) |
| 8 | 2 | `header_bytes` | 64 |
| 10 | 2 | `major` | 1 |
| 12 | 2 | `minor` | 0 |
| 14 | 2 | `isa` | закрытый реестр target ISA |
| 16 | 2 | `abi_major` | User ABI target |
| 18 | 2 | `flags` | unknown required → reject |
| 20 | 8 | `object_bytes` | полный размер файла |
| 28 | 8 | `section_dir_offset` | checked file range |
| 36 | 4 | `section_count` | bounded count |
| 40 | 8 | `reloc_table_offset` | checked file range |
| 48 | 8 | `reloc_table_bytes` | bounded size |
| 56 | 8 | `reserved_zero` | обязательно 0 |
| **64** | | | **конец заголовка** |

Сумма длин точно **64**. При отсутствии relocation table `reloc_table_offset=0`, `reloc_table_bytes=0` и `section_count` остаётся определённым. `object_bytes` обязан совпадать с фактической длиной; owned file ranges не пересекаются, без неописанных overlay. Полная побайтовая форма записей секций и CREL-подобного потока ещё подлежит golden freeze, поэтому сам `.ivo` пока не FROZEN, хотя базовый заголовок определён однозначно.

### 5.3. Релокации

CREL-подобные дельта-операции с канонической varint serialization; релоцируемые места отсортированы и не пересекаются. Тип relocation закрыт по ISA: local PC-relative, import call, data ref, TLS placement, vtable-relative и bounded veneer. Никакой скриптовой «произвольной замены машинных байтов» в runtime IVX. Linker обязан output canonical machine content и verify range of branch instructions. Function-address identity учитывает taken-address; это ограничивает safe ICF.

### 5.4. Safe ICF / COMDAT

Raw byte identity недостаточна при разных relocation targets, ownership semantics и CFI. Допустимо слияние графов только при проверенном semantic equivalence и отсутствии observable address identity; duplicate strong symbol вне согласованной COMDAT — ошибка. Order-independent link — обязательная цель.

## 6. `.ivl` — разделяемая нативная библиотека

Использует **то же IVX-1 binary image family**, что и `.ivx`, но `image_kind=SHARED`, `entry_rva=0` и требует полные `EXPORTS/INTERFACE_TYPES`. Самостоятельно запускать её как приложение запрещено; signed library содержит immutable TEXT_HOT/TEXT_COLD/RO_PURE/RO_REL и приватные BIND/DATA/TLS шаблоны. Публичные symbols имеют стабильные `interface_id`/`signature_id`, права и ABI-major. Локальный interposition/LD_PRELOAD отсутствует. Side-by-side версии разрешаются Install Manager в `Closure Record`, не по поиску первого совпавшего soname.

**Важное:** нативная `.ivl`, отображённая в адресное пространство процесса, НЕ является sandbox или аппаратно изолированным компонентом сама по себе. Для недоверенных plugins используется отдельный процесс с typed IPC и scope-limited capabilities.

## 7. `.ivpkg` — контейнер распространения

### 7.1. Назначение

Пакет переносит полный набор отдельно подписанных `.ivx`/`.ivl` под разные ISA, installer metadata, runtime resources, licenses, content digests и SBOM. Сам `.ivpkg` **не отображается как исполняемый**; его parser получает bounded compressed stream, проверяет размеры/хеши/сигнатуры, затем атомарно переносит immutable artifacts в verified store.

### 7.2. Proposed chunk-index container

Вместо zip с path traversal и неограниченной декомпрессией предлагаем canonical container с header `IVPKG1`, плоским отсортированным manifest/index и typed blobs `EXEC_IMAGE`, `SHARED_IMAGE`, `RESOURCE`, `DEBUG_REFERENCE`, `SBOM_REFERENCE`, `PROVENANCE`. Каждый blob имеет content hash и explicit compressed/plain lengths. Кодек v1: `NONE` и bounded `ZSTD` только для **доставки**, не для единого runtime executable page stream. Container path допускает только нормализованные relative paths без `..`, NUL, абсолютных путей или symlink extraction outside installation root.

### 7.3. Подпись и атомарная установка

Package signature подтверждает manifest/набор blobs, но Install Manager независимо проверяет **каждый** IVX image signature/root и политику publisher. Порядок: STAGED → VERIFIED → RESOLVED → COMMITTED → ACTIVE. Partial install не попадает в active closure; старое состояние остаётся работоспособным. Пакет не может молча подменить системный root key, loader или BootInfo.

## 8. `.ivdbg` — отдельная отладочная информация

Хранит symbol/function fragment maps, source line tables, compile units, types, inlining provenance, sourcemap and optional split DWARF data для совместимости host tools. В runtime image не отображается по умолчанию. Debug package имеет `related_ivx_content_root:32 bytes` и собственный `debug_root`; debugger проверяет точное соответствие идентичности образа перед symbolization. Время сборки/дополнительные исходные пути могут жить в debug data, но должны проходить sanitization/privacy policy. Нельзя подключать «похожий» `.ivdbg` по имени процесса.

## 9. Build receipt и SBOM

Отдельные необязательные release-artifacts: signed build receipt и SPDX/CycloneDX SBOM; оба ссылаются на exact content root IVX. Они не являются выполненными программами и не обязаны отображаться загрузчиком. Никакого circular root: debug/package/build receipt может ссылаться на IVX, но signed IVX не включает свой полный package hash как обязательное поле.

## 10. Файловый жизненный цикл и пример

```
myapp/
  vibe.project.toml
  vibe.lock
  src/
    main.vibe
  build/                   # generated, ignored by source control
    ir/main.vsg
    ir/main.vbir
    obj/main.ivo
    bin/myapp.ivx
    debug/myapp.ivdbg
  dist/
    myapp-x86_64.ivpkg
```

`vibe build` → canonical inputs → VSG → VBIR → `.ivo` → `.ivx`; `vibe package` пакует release артефакты с отдельно утверждаемой подписью. Промежуточные файлы удаляемы и могут пересоздаваться; не должны быть source-of-truth для прав или версии.

## 11. Соответствие и отрицательные тесты

- `.ivx` с `image_kind=SHARED` под именем app.ivx → reject as app.
- `.ivl` с `entry_rva!=0` → reject.
- `.ivo` с duplicate relocation/invalid delta/out-of-range symbol → reject.
- `vibe.lock` с ambiguous dependency digest → reject.
- `.ivpkg` с `../../` archive path, decompression bomb, duplicate case-folded destination → reject.
- `.ivdbg` с другим `content_root` → нельзя символизировать runtime dump.
- `.vibe` с invalid UTF-8 и неразрешённым identifier → parser error.
- Никакой файл семейства не предоставляет привилегии простой записью `capability=allow`.

## 12. Открытые поля перед freeze

Полные byte layouts `IVO1` и `IVPKG1`, record IDs, varint encoding, compression constraints, crypto domain separators, canonical TOML normalization, debug index encoding, complete ABI/symbol schema и golden fixtures требуют отдельной реализации двух readers. Здесь указаны роли и семантические ограничения; формат не следует считать READY для загрузки в ядро.
