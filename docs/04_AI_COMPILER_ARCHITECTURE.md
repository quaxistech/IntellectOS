# ИИ-компилятор IntellectOS — архитектура Vibelang → IVX

**Имя компилятора:** пока **не выбрано**. `AI Compiler` — временное обозначение. **Версия архитектуры:** 1.0 DRAFT. **Назначение:** воспроизводимая сборка `.vibe` в нативные `.ivx` и `.ivl` с детерминированной проверкой, AI-assisted генерацией и профильной оптимизацией.

## 1. Основной принцип

Компилятор имеет обязательный **детерминированный путь** и необязательную интеллектуальную плоскость. Последняя создаёт предложения, но никогда не является последней инстанцией корректности машинного кода.

```
.vibe + vibe.project.toml + vibe.lock
                 │
        Lexer → Parser → CST/AST
                 │
         Resolver/Name binder
                 ▼
          VSG builder
                 │  type/effect/ownership verifier
                 ▼
            VALID VSG
                 │  lowering and validation
                 ▼
             VBIR SSA
                 │  optimizer / verified AI candidates
                 ▼
       optimized VALID VBIR
                 │  ISA lowering / register allocation / assembler
                 ▼
             .ivo objects
                 │  IVX linker / ABI / relocation verifier
                 ▼
          unsigned canonical IVX
                 │  IVX parser + Merkle + signing service
                 ▼
             signed .ivx
```

**AI branch:** user intent/metrics → planner → candidate source patch / VSG transform / VBIR transform / layout policy → canonical parsing → validation → differential tests → benchmark → accept/reject. Модель никогда не отправляет сырой код непосредственно в память ядра. `AI unavailable` ⇒ обычный компилятор продолжает работу.

## 2. Компоненты

| Компонент | Вход | Выход | Доверие |
|---|---|---|---|
| `project-reader` | manifest, lock, sources | build input graph | детерминированный |
| `syntax-front` | UTF-8 `.vibe` | CST/AST | детерминированный |
| `semantic-engine` | AST/imports | VSG | детерминированный |
| `vsg-verifier` | VSG | typing/effect/ownership verdict | независимый от ИИ |
| `vbir-lowerer` | valid VSG | VBIR | детерминированный |
| `vbir-verifier` | VBIR | SSA/effects/control-flow verdict | независимый от ИИ |
| `optimizer` | legal VBIR, profile | VBIR transform results | проверяемый |
| `isa-backend` | VBIR+target | machine sections/.ivo | ISA-specific |
| `ivx-linker` | .ivo, ABI | canonical unsigned image | детерминированный |
| `ivx-verifier` | full image | pass/fail | отдельно от linker |
| `signing-interface` | verified digest, authorization | signature envelope | доверенный key service |
| `ai-adviser` | source/metric context | proposals | **недоверенный** |
| `benchmark-runner` | multiple valid builds | measured score | изолированная среда |

Сначала это могут быть модули одного процесса. Логическое разделение обязательно; десятки отдельных демонов на этапе MVP не нужны.

## 3. Build identity

Ключ сборки составляют source content root, canonical `vibe.project.toml`, `vibe.lock`, target triple, stdlib version, compiler binary digest, optimizer pass configuration, profile and optional PGO hash. Timestamp, absolute host path и произвольный ответ модели вне *принятого* source patch не должны менять IVX content root.

`BuildReceiptV1`: input digests, toolchain identity, target, applied pass digests, VSG/VBIR roots, object roots, IVX content root, test/benchmark verdicts, signer reference. Запись не включает секретный ключ. Понятие воспроизводимости проверяется повторной сборкой и побайтовым сравнением, а не только одинаковым консольным сообщением.

## 4. Frontend

Lexer фиксирует UTF-8, source spans и комментарии; parser строит детерминированное дерево. Name resolver читает только указанное dependency graph и не ищет скрытые библиотеки через системное окружение. VSG сохраняет стабильные node IDs, типы, области видимости, эффекты, данные ownership. Диагностики `PARSE`, `NAME`, `TYPE`, `EFFECT`, `BORROW`, `ABI`, `CONTROLFLOW` содержат source location и стабильный машинный code.

## 5. VSG verifier

Проверяет отсутствие неразрешённых имён, полноту type inference, уникальность экспортов, корректное наследование traits и generics, borrow/alias ограничения, транзитивные эффекты, заявленные права, законность unsafe границ, допустимые размеры и UTF-8, integer bounds. Сгенерированный ИИ граф не принимается без канонического источника либо отдельно разрешённого проверяемого преобразования с base hashes.

## 6. VBIR и verifier

VBIR — typed SSA control-flow graph. Для каждого basic block: идентификатор, параметры, инструкции, ровно один terminator. Проверять dominance/def-use, одно определение каждого value, типы/ширину, control-flow edges, memory ordering, lifetimes, effect tokens, capability handle provenance, диапазоны массивов и stack-frame rules. После AI-патча verifier запускается снова. «Проверенная SSA-структура» не доказывает полную семантическую эквивалентность изменения.

## 7. Классы предложений ИИ

**SOURCE_EDIT:** патч `.vibe` с `base_root` и expected source behavior; проверяется всеми frontend passes, tests, при изменении поведения требуется review.

**IR_TRANSFORM:** изменённые instructions/blocks, formal preconditions и список effects. Verification — structural/type safety и translation validation для supported fragment; при отсутствии доказательства используется тестовый эксперимент с явной маркировкой его ограниченной доказательной силы.

**LAYOUT_POLICY:** hot/cold placement, alignment budget, link-group hints на основе PGO. Linker обязан проверить unwind fragments, CFI, branch ranges и immutable TEXT.

**BUILD_PROFILE:** FASTMAP/BALANCED/COMPACT, ThinLTO, CPU variants. Каждая альтернатива повторно проверяется и сравнивается по benchmark. Предложение не меняет системные права.

## 8. Недопустимые действия ИИ

Запрещены: пропуск диагностики; генерация release-подписи; отключение memory safety без доказуемого эквивалентного механизма; подмена зависимости в lockfile без authorizer; сокрытие регрессии в тестах; изменение trusted roots; доступ к private signing key; самостоятельная запись в ядро; отправка исходников во внешний сервис без согласия; пометка `PROVEN` на основании собственного объяснения.

## 9. Нативные оптимизации

IR: constant folding, DCE, CSE, copy propagation, loop invariant movement, strength reduction, escape analysis, vectorization и bounds-check elimination при проверяемых условиях. Machine: register allocation, instruction scheduling, branch relaxation, interprocedural inlining в разрешённых границах, safe identical code folding, relative pointers, explicit TLS, hot/cold profile guided layout, оптимизация i-cache/iTLB. Недопустимо нарушать function pointer identity, CFI, unwind или публичный ABI при Safe ICF.

## 10. Степени доказательности

| Grade | Что известно | Что НЕ утверждается |
|---|---|---|
| STRUCTURAL | прошёл парсер и типы/SSA | полная функциональная корректность |
| TESTED | прошёл набор тестов | эквивалентность на всех входах |
| DIFFERENTIAL | совпал с reference на заданных входах | теорема о всех программах |
| PROVEN_LOCAL | проверяемое доказательство ограниченного преобразования | правильность всей системы |
| RELEASE_VERIFIED | проверены IVX, подпись и release policy | отсутствие всех уязвимостей |

Верификация типов, владения и интерфейсов не заменяет kernel isolation. Signed binary не является математическим доказательством корректности.

## 11. Parallel/incremental compilation

Входные объекты обрабатываются параллельно с детерминированным порядком финального output. Cache key включает source/dependency/toolchain/target/pass digests. Дубликат strong symbol — ошибка; COMDAT складывается только при корректной семантической эквивалентности; linker не зависит от порядка ввода архивов. Для больших сборок optional ThinLTO summary отдельно от object content.

## 12. Target и backend

Начальная ISA x86-64: она соответствует действующему native kernel на UEFI, но **это не означает наличия Vibelang backend**. Future: AArch64 отдельным портом. Target profile включает ISA, calling convention, OS user ABI, required CPU features, page size, security policy и stdlib ABI. Один `.ivx` — одна ISA; варианты для нескольких машин доставляются отдельными `.ivx` внутри `.ivpkg`.

## 13. Собственный IVX linker

`.ivo` содержит объекты и relocation data, Basic Block Address Map, addrsig, function identities и provenance. Linker разрешает локальные вызовы, оформляет external imports, segment layout, compact fixups, ABI hashes, CFI, unwind и manifest. Он выдаёт **unsigned canonical content region**; независимый IVX validator проверяет байтовые инварианты; далее Merkle, подпись через авторизованный key-service и повторная проверка полного файла. Никакого trust на основании «linker сам всё правильно сделал».

## 14. Предлагаемые CLI-команды

```bash
vibe check --project vibe.project.toml
vibe build --project vibe.project.toml --target x86_64-intellectos --profile balanced
vibe test --project vibe.project.toml
vibe inspect --artifact build/app.ivx --json
vibe verify --artifact build/app.ivx
```

`vibe` — проектное имя интерфейса toolchain, **не выбранное имя нашего ИИ-компилятора**. Команды на момент проектирования не реализованы. Error classes: `PARSE`, `TYPE`, `BORROW`, `EFFECT`, `IR_INVALID`, `LOWERING`, `LINK`, `IVX_LAYOUT`, `SIGNATURE`, `POLICY`, `UNSUPPORTED_TARGET`, `TEST_FAILURE`, `TOOLCHAIN_INTERNAL`.

## 15. Bootstrap на существующей рабочей станции

Пока у IntellectOS нет пользовательского пространства, компилятор работает на Ubuntu 24.04 как cross toolchain. На раннем этапе допустим LLVM backend или временные ELF `.o` **только как внутренний build representation**, но финальный output должен быть самостоятельным IVX с независимой проверкой. Переименовать ELF в `.ivx` недопустимо. BootInfo/UEFI PE32+ в текущем проекте не требуют миграции в Vibelang.

## 16. Устойчивость и supply chain

Staging dir + atomic rename; повреждённый cache invalidates by digest; прерванная сборка не публикует неполный `.ivx`. SDK и compiler binaries закреплены digest; third-party passes ограничены и запускаются в sandbox. Release signing отдельно от AI/optimizer. SBOM, build provenance и полные debug data — отдельные объекты, связанные с IVX content root. Никаких токенов/ключей в prompts и logs.

## 17. Benchmark и proof-of-value

Замерять на одинаковой ISA/hardware: cold/warm launch p50/p95/p99, total bytes, mapped/shared/private pages, CPU instructions, iTLB/I-cache, branches, tail latency, compile time, peak RAM, energy, rollback success. Сравнивать честные baseline на ELF/PE/Mach-O при близких компиляторных оптимизациях; различать выигрыш формата файла и результат иного optimizer.

## 18. Этапы реализации

C0 lexer/parser; C1 VSG serializer/verifier; C2 VBIR SSA/verifier; C3 x86-64 backend/.ivo; C4 IVX linker и offline validator; C5 static signed `.ivx`; C6 imports/types/TLS; C7 optimization/PGO; C8 AI proposals with deterministic replay; C9 native developer tooling IntellectOS. Для каждого этапа необходимы fixed test vectors и falsifiable acceptance. AI не является условием базовой компиляции.
