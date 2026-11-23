# VSG и VBIR — промежуточные форматы компилятора Vibelang

**Редакция:** 1.0 DRAFT · **Назначение:** независимая проверка смысла программы и машинно-ориентированных оптимизаций до генерации `.ivo`/`.ivx`. **Статус:** проектируемые форматы; ядро IntellectOS не читает и не исполняет их.

## 1. Общая цепочка и терминология

`Vibe Source (.vibe) → parser/CST/AST → VSG (.vsg) → semantic verifier → VBIR (.vbir) → SSA verifier → optimizer → backend .ivo → IVX linker → .ivx`.

**VSG — Vibelang Semantic Graph:** не синтаксическая копия исходного текста, а нормализованный типизированный граф имён, областей видимости, деклараций, эффектов, ограничений владения и source provenance. **VBIR — Vibelang Binary Intermediate Representation:** типизированное низкоуровневое SSA IR с явными переходами, памятью, эффектами и ABI-lowering annotations. `Binary` означает конечную сериализацию для машинной обработки, **не нативный машинный код**.

Инструментальный **text dump** `.vsg.txt` или `.vbir.txt` — только диагностика и diff, не нормативный серийный payload. В v1 канонический `*.vsg`/`*.vbir` не зависит от timestamp, абсолютного пути, AI промпта или адреса процесса.

## 2. Структурный envelope (предлагаемая побайтовая основа)

Оба формата используют отдельный магический префикс и общую форму заголовка **64 байта**, все поля little-endian, фиксированного размера; layout ниже — **проект для freeze fixtures**, пока нет эталонного encoder.

| Offset | Size | Type | Правило |
|---:|---:|---|---|
| 0 | 8 | `magic[8]` | VSG=`VSG1\0\r\n\x1a`, VBIR=`VBIR1\r\n\x1a` — 8 байт; буквальные тестовые байты фиксировать fixture |
| 8 | 2 | `header_size` | `64` |
| 10 | 2 | `major` | `1` |
| 12 | 2 | `minor` | `0` |
| 14 | 2 | `flags` | неизвестные mandatory биты запрещены |
| 16 | 8 | `total_bytes` | полный размер без неописанного хвоста |
| 24 | 8 | `table_directory_offset` | только checked range |
| 32 | 4 | `table_directory_count` | конечный upper bound |
| 36 | 4 | `table_entry_bytes` | `24` |
| 40 | 16 | `source_identity_prefix` | первые 16 байт полного BLAKE3 source root |
| 56 | 8 | `reserved` | MUST zero |

**Важная граница:** 16-байтное поле лишь быстрый identifier prefix, а **полный** `source_root` хранится в required `BUILD_IDENTITY` table. Сокращённый префикс нельзя использовать как криптографическое доказательство равенства двух сборок. Показанные escape-представления magic требуют byte fixture перед заморозкой, поскольку люди могут иначе понять текстовую запись; все читатели после freeze сравнивают байты, а не строковый literal из Markdown.

### 2.1. TableDirectoryEntryV1 — 24 байта

`kind:u32 | flags:u32 | file_offset:u64 | byte_length:u64`. Сортировка `(kind,file_offset)` обязательна; нет перекрытий независимых owned ranges. `REQUIRED` неизвестный kind — отказ, optional non-semantic debug record можно пропустить после полной range проверки. Кодировки целых: `u8/u16/u32/u64`, signed через two's complement. Все `record_count` и `array_len` включают upper bounds.

### 2.2. Строки, идентичности, ссылки

В таблицах `StringPoolV1` каждый элемент `(byte_length:u32, utf8_bytes)`; null terminator не включён, UTF-8 проверен, имена экспортов нормализованы как ASCII v1, никаких case-insensitive aliases. Ссылка на сущность задаётся `u32` индексом таблицы либо `u64` stable node identifier; **не** сырым указателем на память компилятора. Канонический ID строится из namespace, declaration kind, canonical type signature, source-order-independent local disambiguator и path-independent source root. Связь между графами — по полному identity, не по указателям.

## 3. VSG — декларативные таблицы

| Таблица | Содержит | Обязательность |
|---|---|---|
| `BUILD_IDENTITY` | полный исходный digest, toolchain, target language | required |
| `STRINGS` | UTF-8 текстовые идентификаторы | required |
| `MODULES` | module IDs, imports, declared caps | required |
| `DECLARATIONS` | functions, constants, types, exports | required |
| `TYPES` | canonical type DAG, sizes if resolved | required |
| `SYMBOL_LINKS` | references to declarations/type uses | required |
| `SCOPES` | lexical scopes, parent links | required |
| `EFFECTS` | per function declared/inferred effects | required |
| `OWNERSHIP` | move/borrow/regions and constraints | required when borrows exist |
| `SOURCE_MAP` | spans, normalized file IDs | required for debug builds |
| `CONTRACTS` | optional checked static obligations | conditional |
| `DIAGNOSTIC_HINTS` | nonsemantic tooling data | optional |

### 3.1. VSG invariants

Every declaration has exactly one semantic owner; no module graph dangling edges, duplicate exported IDs, cycles in type layout by value, impossible generic substitutions or out-of-range source spans. Type graph may contain recursion **only** behind explicit indirection; no infinite-size values. Effects are stable sorted sets; under-declaration of transitive effects is invalid. Borrow graph cannot allow two live conflicting mutable aliases. Capabilities are requirements, not actual live kernel handles. Source map locations cannot alter type semantics.

### 3.2. Пример VSG text dump

```text
module #1 demo.calc
fn #31 add(x:i64, y:i64) -> i64
  visibility: public
  effects: {}
  ownership: args by_value
  body_expr: add_i64_checked(#x,#y)
  source_span: (file#1, line 3, columns 1..41)
```

Это **не нормативные бинарные байты**; они описывают смысл, который VSG-verifier обязан подтвердить независимо от ИИ.

## 4. VBIR — нормальная форма SSA

У каждой функции существуют `FunctionId`, `SignatureId`, `Effects`, `Blocks`, `ValueId` и `Terminator`. `Block` допускает параметры-phi в форме block arguments. Каждая инструкция определяет один или несколько типизированных результатов; `ValueId` определён ровно один раз и доминирует все использования. Control-flow переход передаёт аргументы ровно соответствующих типов/числа.

### 4.1. Типы VBIR

`i1/i8/i16/i32/i64`, `u*` where signedness is attached to op, `f32/f64`, `vector(element,count)`, `ptr(region,provenance,rights,alignment)`, `handle(interface,rights)`, `tuple`, `aggregate`, `effect_token(kind)`, `memory_token(region)`, `never`. Физический raw pointer допустим только после explicit lowering of an unsafe/FFI region и сохраняет policy fence, если требуется.

### 4.2. Операции (закрытый стартовый каталог)

| Категория | Операции | Верификация |
|---|---|---|
| Константы | `const_int`, `const_float`, `const_null_handle` | ширина/диапазон/тип |
| Arithmetic | `add_checked`, `sub_checked`, `mul_checked`, `div_checked`, `add_wrap` | overflow semantics |
| Logic | `cmp_eq`, `cmp_lt_s/u`, `and`, `or`, `not`, `select` | scalar/vector types |
| Stack | `stack_alloc`, `lifetime.start/end` | alignment, escape, lifetime |
| Memory | `load`, `store`, `memcpy_bounded`, `bounds_check` | provenance, memory token, access rights |
| Handles | `cap_check`, `cap_narrow`, `cap_pass` | не расширять права |
| Calls | `call_direct`, `call_import`, `call_indirect_checked` | signature/effects/CFI |
| CFG | `br`, `cond_br`, `switch`, `return`, `trap`, `unreachable` | terminator exactly one |
| Atomic | `atomic_load/store/rmw`, `fence` | only after atomics ABI gate |
| Syscall | `syscall_typed` | only known SDK interface/effects |

Неизвестный required opcode, invalid operand, duplicate block, unsupported instruction semantics — отказ. Использовать «generic execute-bytecode from file» в ядре запрещено. Это IR только компилятора и независимого verifier.

### 4.3. Пример VBIR text dump

```text
fn @demo.calc.add(%x:i64, %y:i64) -> i64 effects() {
block0():
  %sum, %overflow = add_checked.i64 %x, %y
  cond_br %overflow, block_error(), block_ok(%sum)
block_error():
  trap ARITHMETIC_OVERFLOW
block_ok(%result:i64):
  return %result
}
```

VBIR-verifier проверяет, что `%result` и `%sum` имеют один тип, `%overflow` определён, аргументы блоков согласованы, а terminating trap не допускает fallthrough в неинициализированный участок.

## 5. Структурные обязательства VBIR

1. Упорядоченный graph функций имеет уникальные идентификаторы и точное число входов/выходов.
2. CFG блоки не содержат несколько terminators и не оставляют неограниченных falling-through путей.
3. Def-use/Dominance: каждое использование имеет валидное определение в подходящем предшественнике.
4. Memory/Effect tokens делают изменения наблюдаемого порядка операций явными.
5. Уход из borrow region без release / move-after-use отвергается.
6. `unsafe` не создаёт новый capability permission — только разрешённую type-level область для native FFI.
7. Отсутствует опасная нормализация `u64` адреса в pointer без provenance proof.
8. Every import has an exact SignatureId and effective rights upper bound.
9. Generic instantiation bounded по recursion depth и overall IR size.
10. Deterministic serialization для одинакового semantic graph и target configuration.

## 6. Оптимизационные сертификаты

`PassReceiptV1`: `input_root`, `pass_name`, `pass_version`, `config_hash`, `output_root`, `verifier_digest`, `verification_grade`, optional `proof_artifact_root`. ИИ может сообщить `claimed_equivalence`, но это не заменяет grade. Для локальных transforms возможно machine-checkable proof по ограниченной формальной модели; для остальных — обособленные differential/property tests и репрезентативная нагрузка.

Важный инвариант: `PASS` structural verifier после оптимизации **не равен** доказательству эквивалентности. При выпуске compiler должна действовать политика, которая допускает только заранее реализованные и проверенные алгоритмы оптимизаций, а экспериментальные AI transforms запускает в сравнительной среде.

## 7. Переход VBIR → ISA codegen

Compiler backend назначает target widths/registers, lowers ABI types, resolves instructions (including call ranges), emits unwind fragment metadata, CFI targets, static TLS offsets, explicit import call sites и format-neutral relocations в `.ivo`. Правила lowering должны поддерживать semantics checked arithmetic, division edge cases, overflow flags, NaN, memory alignment и concurrency. Если платформе не хватает инструкции, backend использует корректный sequence или выдаёт `UNSUPPORTED_TARGET`, а не обрезает значение.

## 8. Версионирование и feature gates

VSG и VBIR **отдельно** имеют `major/minor` и required-feature sets. Компилятор не интерпретирует новое значение старого opcode по-другому без несовместимой версии. Новое optional debug metadata не меняет семантику. Выбор нового compiler model не меняет семантику IR. Должна быть матрица `Vibe Language → VSG → VBIR → Target ABI → IVX` с проверкой несовместимости до генерации образа.

## 9. Вредоносный ввод и лимиты

Файлы IR рассматриваются как недоверенные, даже если создавались «своим» компилятором. Parser использует checked arithmetic для всех длин и offsets, ограничивает число таблиц, узлов, инструкций, строк, CFG edges, глубину типа, общий размер и время верификации. Повторяющиеся references, графы с непрерывным разрастанием и hash collision attacks должны диагностироваться без OOM/stack overflow. Никаких runtime plugins из `.vsg`/`.vbir`.

## 10. Проверка на завершение V1

- Golden parse/re-encode byte fixtures для заголовка и каждой таблицы.
- Cross-language независимый reader.
- Corpus правильных и неправильных типов, dangling refs, backedges, borrow conflicts и эффектов.
- Тесты одинакового build root при разном обходе source files/разных host paths.
- Round-trip `VSG → dump → parser` не должен использовать dump как доверенный источник без полной проверки.
- Проверка, что IVX loader отвергает магические префиксы VSG/VBIR как исполняемые.

**Примечание:** точные бинарные записи всех таблиц и byte patterns magic требуют отдельной golden фиксации до признания `.vsg`/`.vbir` FROZEN; этот документ задаёт контракт и основу будущей реализации, но не утверждает существующий encoder.
