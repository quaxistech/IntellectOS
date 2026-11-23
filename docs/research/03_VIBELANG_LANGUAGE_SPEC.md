# Vibelang — спецификация языка программирования V1

**Проектный уровень:** Language 1.0 DRAFT · **Статус:** язык ещё не реализован. **Назначение:** выразительный исходный язык IntellectOS, компилируемый через VSG/VBIR в нативные IVX-программы. **Исходное расширение:** `.vibe`.

## 1. Концепция

Vibelang — не «промпт, который CPU исполняет», и не скриптовая обёртка над ИИ. Это строгий компилируемый язык с удобными конструкциями для человека и AI-разработчика. Главные принципы: явные побочные эффекты и полномочия; память с проверяемым владением; типизированные интерфейсы; прозрачная стоимость низкоуровневых действий; детерминированная семантика; нативная компиляция без обязательного сборщика мусора/виртуальной машины; возможность строить и анализировать исходники ИИ-инструментами. Когда компилятор не знает, что означает программа, он **выдаёт ошибку**, а не «угадывает» намерение.

Объявление исходного файла не даёт разрешения на доступ к сети/диску. Исполняемый `.ivx` содержит запрос прав, а IntellectOS при запуске выдаёт конкретные ограниченные handles. Вирусный/ошибочный нативный код не становится безопасным автоматически от наличия эффектной системы: необходимы user isolation, W^X и policy enforcement на стороне ОС.

## 2. Модель версий

`Language 1.0`: лексика, базовые типы, ownership/effects и базовая семантика. `Standard Library API` версионируется независимо от языка, а `IntellectOS User ABI` — независимо от обоих. Источник может указать `language = "1.0"` в `vibe.project.toml`. Несовместимые расширения не «молча включаются» по новой версии компилятора; compiler flags и optional feature gates закреплены build manifest.

## 3. Кодировка и лексика

Файлы `.vibe` — UTF-8 без BOM. Переводы строк `LF`; на входе допустимо `CRLF` с нормализацией *только концов строк*, исходная позиция ошибок сохраняется. Идентификаторы: ASCII `[A-Za-z_][A-Za-z0-9_]*` в базовом v1 — узкая семантика для однозначного ABI и простых компиляторов. Для языковой экосистемы международные имена можно добавить позже после Unicode normalization/confusable policy; строковые литералы поддерживают UTF-8 сейчас.

Комментарии: `//` до конца строки и `/* ... */` без вложения в базовом варианте; doc comments `///` доступны инструментам, не влияют на машинное исполнение. Ключевые слова: `module`, `use`, `requires`, `pub`, `fn`, `let`, `mut`, `const`, `struct`, `enum`, `trait`, `impl`, `if`, `else`, `while`, `for`, `in`, `match`, `return`, `break`, `continue`, `true`, `false`, `as`, `unsafe`, `effects`, `move`, `borrow`, `Result`, `Option`. Последние 2 — предопределённые библиотечные generic-типы; синтаксис резервируется.

Числа: десятичные, `0x`/`0b`, подчёркивание как разделитель разрядов; константное переполнение — ошибка. Строки: UTF-8 в двойных кавычках, явное экранирование, недопустимые Unicode scalar значения отвергаются. Неявного преобразования `i64`↔`u64`, float↔integer и pointer↔integer нет.

## 4. Канонический пример

```vibelang
module demo.hello;
use std.console;
requires { console.write; }

pub fn main(ctx: &mut ProcessContext) -> i32 effects(console.write) {
    console.write_line(ctx.console, "Привет, IntellectOS!");
    return 0;
}
```

`ProcessContext` и `std.console` обозначают **проектный** пользовательский runtime API: на момент написания не являются готовой библиотекой. `ctx.console` — ограниченный capability-handle, не глобальный stdout. `effects(console.write)` — верхнее множество наблюдаемых эффектов; при нехватке прав операция получает диагностируемую ошибку независимо от декларации исходника.

## 5. Грамматика минимального языка (проектная EBNF)

```ebnf
source       = module_decl, {use_decl}, [requires_decl], {item} ;
module_decl  = "module", path, ";" ;
use_decl     = "use", path, ";" ;
requires_decl= "requires", "{", {capability, ";"}, "}" ;
path         = ident, {".", ident} ;
item         = ["pub"], (function | struct_decl | enum_decl | const_decl) ;
function     = "fn", ident, "(", [params], ")", ["->", type],
               ["effects", "(", [effect_list], ")"], block ;
params       = param, {",", param} ;
param        = ident, ":", type ;
block        = "{", {stmt}, "}" ;
stmt         = let_stmt | assign_stmt | return_stmt | if_stmt |
               while_stmt | expr_stmt | block ;
let_stmt     = "let", ["mut"], ident, [":", type], "=", expr, ";" ;
return_stmt  = "return", [expr], ";" ;
expr_stmt    = expr, ";" ;
type         = ident | "&", ["mut"], type | "[", type, ";", integer, "]" ;
```

Это **ядро синтаксиса**, не полная формальная грамматика. При выпуске языка обязательны таблица приоритетов и ассоциативности, полные grammar fixtures, проверка однозначности parse и error recovery без изменения semantics.

## 6. Выражения и операторы

- Арифметика `+ - * / %`: базовый безопасный режим с проверкой переполнения; `wrapping_*` и `saturating_*` являются явными операциями.
- Сравнение `== != < <= > >=`, short-circuit `&& || !`, битовые `& | ^ ~ << >>` с проверкой допустимого размера сдвига.
- Присваивание только `mut`-месту, не смешивается с объявлением переменной.
- `if`, `while`, `match`; все ветви выражения имеют совместимый тип, варианты enum проверяются на исчерпываемость.
- Приведение между непрозрачным kernel handle и числом не порождает право доступа.

## 7. Типовая система

Примитивы: `bool`, `i8/i16/i32/i64`, `u8/u16/u32/u64`, `isize/usize` (с ABI-документированным размером), `f32/f64` (IEEE-754), `char` (Unicode scalar), `unit` и `never`. `i128/u128` — опционально после поддержки компилятором/ABI.

Композиции: `[T; N]`, `Slice<T>`, tuple, `struct`, `enum`, `Option<T>`, `Result<T,E>`, `&T`, `&mut T`, `Box<T>` и `Handle<T,Rights>`. Nullable pointers не являются безопасным типом по умолчанию; отсутствие значения выражается `Option`. Внутренний `pub struct` не обещает C-layout, пока не задан ABI-mode. Публичный IVX interface содержит каноническую схему параметров, результатов, собственности и effects.

## 8. Владение и lifetime

- Значение имеет владельца; move передаёт ownership; use-after-move — compile error.
- `&T` допускает конкурентные неизменяемые borrow, `&mut T` — единственный изменяемый.
- Ссылка не переживает владельца; возвращение ссылки на stack-local переменную — ошибка.
- Unsafe FFI требует отдельного блока и ABI-предусловий, но не отменяет границы OS privileges.
- `FileHandle`, `ChannelHandle`, `MemoryRegion` управляются scoped lifetime/RAII-подобным освобождением.
- Межпоточные типы должны удовлетворять проверяемым контрактам передачи/совместного доступа.

Borrow checker формируется детерминированно. Модель ИИ может предложить другой код, но не объявить недопустимый alias безопасным.

## 9. Effect system

`effects(console.write, fs.read, net.connect)` — транзитивная статическая верхняя граница effects функции. Если reachable callee требует новый effect, он должен входить в контракт вызывающего API. `pure` функция не исполняет IO, таймер, внешний mutable state и привилегированную операцию. Даже при ошибке статической проверки действующий kernel проверяет actual capability handle на каждой операции.

`requires { fs.read; }` в модуле формирует **только запрос**, не грант. Грант — результат policy intersection при запуске IntellectOS. Наличие пути `/etc/...` в строке исходника само по себе ничего не разрешает.

## 10. Модули и линковка

`module foo.bar;` задаёт уникальную область имён; `use std.console;` связывает API по типизированной compile-time декларации. Пакет зависимостей фиксирует build manager и `vibe.lock`; `ivx-linker` оформляет imports/exports по interface hash, а Install Manager закрепляет конкретные версии в Closure Record. Никакого поиска «первой библиотеки с таким mangled символом».

## 11. Контракты и ошибки

`assert` — runtime check по определённой модели traps; `debug_assert` не заменяет обязательную проверку памяти/прав. Будущие `requires/ensures` для функций могут создавать SMT obligations; если доказательство отсутствует, security-critical check сохраняется или сборка отвергается. Ошибки возвращать через `Result<T,E>`; panic для нарушенных инвариантов, а не штатного отсутствия файла. Межмодульное unwind исключение запрещено до фиксации ABI исключений.

## 12. Конкурентность и асинхронность

MVP не требует ни async/await, ни garbage collector. При появлении scheduler добавляются native threads/channels с безопасным ownership; atomic memory orders и правила data-race определяются отдельным контрактом. API памяти вне safe region не считается thread-safe из-за слова `shared`. Async/await и actor model — последующие расширения, не фактическая возможность существующего compiler.

## 13. ABI и FFI

Публичные ABI-типы: `FROZEN_LAYOUT` для фиксированных POD и `RESILIENT_INTERFACE` для evolving API. `type_hash/signature_id` включает calling convention, ownership, alignments и эффекты. C/Rust совместимы только через заданный адаптер с явной `repr`, ABI, error handling и lifetime. Бинарный pointer или function pointer нельзя переносить как постоянный идентификатор между процессами.

## 14. Аннотации (проект)

`@entry`, `@inline`, `@cold`, `@repr(stable)`, `@repr(c)`, `@test`, `@unsafe` и `@no_mangle` предлагаются с closed schema, но фактически поддерживаются только после parser/type/ABI fixture. Актуальные аннотации должны иметь точно описанные область действия и условия совместимости. `@unsafe` не отключает цифровую подпись или W^X.

## 15. Воспроизводимость и диагностика

Для одинаковых source bytes, dependency lock, toolchain digest, target ABI и pass configuration компилятор обязан воспроизводить unsigned content root. Никакие timestamps, host paths и случайные UUID в нативный IVX не попадают. Ошибка содержит stable code, source span, expected/got, related types, import chain и предложенную правку как необязательную подсказку. Исправления ИИ проходят повторно все обычные проверки.

## 16. Инструменты

Предлагаются `vibe fmt`, `vibe check`, `vibe build`, `vibe test`, `vibe inspect`, `vibe explain`. `vibe` — рабочий CLI, а не принятое собственное имя ИИ-компилятора. Host-first разработка под Ubuntu 24.04 обеспечивает cross-build до появления пользовательского режима IntellectOS.

## 17. Минимальные отрицательные примеры

```vibelang
let x: u8 = 300;   // ошибка константы: вне диапазона u8
let y: i32 = 4;
let z: u32 = y;    // ошибка неявного signed-to-unsigned cast
```

```vibelang
fn invalid_borrow() -> &i32 {
    let local = 7;
    return &local;   // нельзя выдать ссылку на локальную переменную
}
```

## 18. Этапы реализации

L0 — lexer/parser; L1 — AST/VSG names+basic types; L2 — VBIR and x86-64 numeric backend; L3 — structs/refs and borrow checker; L4 — effects/handles/imports; L5 — standard library/process runtime; L6 — optimization/AI-assisted proposals; L7 — concurrency/async. Статус FROZEN потребует полного EBNF, тестов форматов, ABI, сотен положительных и отрицательных программ и независимого parser/verifier.
