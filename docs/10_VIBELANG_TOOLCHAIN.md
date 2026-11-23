# Vibelang и каноническая цепочка компиляции

## 1. Язык `.vibe`

Vibelang — новый строгий native compiled programming language. **Не** natural-language prompt interpreter и не AI runtime. Исходник UTF-8, детерминированное значение syntax/semantics, понятные ошибки компилятора. Интерпретация текстового пожелания человека допустима в отдельной AI-модели, затем результат фиксируется в проверяемом исходном коде и проходит обычную toolchain.

### Базовые свойства

Лексер/parser, CST/AST, модули/packages/import resolution, identifiers and namespaces, functions, local variables, constants, structured control flow, generics/traits when formally specified, algebraic/result types and explicit errors. Типизация статическая и строгая; арифметика и overflow определяются в ABI. Значения имеют ownership; borrow/move и lifetime errors проверяются до backend; `unsafe` bounded, не предоставляет OS capabilities. Effects (`fs.read`, `fs.write`, `net.connect`, `process.spawn`, `device.*`) видимы компилятору, образ requests rights, OS выдает только разрешённые handles. Конкурентность/async и атомики требуют точного memory model.

Простой `.vibe` должен компилироваться **без нейросети**, а generated code и diagnostics быть воспроизводимыми. Vibelang API и язык версионируются отдельно от Kernel/User ABI. FFI с C/другими ABI только при explicit layout/alignment/lifetime/unsafe and architecture rules.

## 2. Схема инструментов

```text
.vibe + vibe.project.toml + vibe.lock
    ↓ lex/parse → AST → binding/name resolution
VSG (*.vsg) → independent type/effect/ownership verifier
    ↓ lowering with semantic proof/checked invariants
VBIR (*.vbir) typed SSA → verifier → optimizer → verifier
    ↓ native backend/ISA lowering, register allocation
IVO (*.ivo) relocatable objects → IVX linker → canonical image
    ↓ independent IVX parser, Merkle hash, signer, package manager
IVX (*.ivx) executable | IVL (*.ivl) shared library
    ↓ trusted install manager → process manager at runtime
```

VSG (Vibelang Semantic Graph) stores typed/normalized graph of entities, effects, permissions, ownership and provenance; VBIR (Vibelang Binary Intermediate Representation) is machine-oriented typed SSA with explicit memory/control/effects. Neither is executable by kernel. Binary envelope/header and table IDs are provisional until golden fixture freeze: test text dump is not signed canonical bytes.

## 3. Сопутствующие артефакты

- `vibe.project.toml` = project metadata; `vibe.lock` = exact reproducible dependency graph.
- `.ivo` = relocatable object (sections, symbols, relocations, import requirements), not user executable.
- `.ivl` = signed native library image; only `SHARED`, no direct process start.
- `.ivpkg` = signed chunk-index package with optional target slices, manifest, dependencies, metadata and atomic activation transaction.
- `.ivdbg` = detached debug/symbol/source mapping bound to full executable content root.
- Full build identity = hashes of source + compiler versions + target flags + dependencies + ambient seed + lock. 16-byte digest prefixes never prove full identity.

Do not revive older unused extensions `.vbin`, `.vobj`, `.vlib`, `.vpkg`, `.vdbg`; canonical `.ivx` remains primary program artifact.

## 4. Semantic verification and performance

VSG verifier: all references resolve, each effect declared, ownership cannot alias mutable references unsafely, interface rights narrowed, no illegal implicit IO. VBIR verifier: SSA dominance, valid phi/control flow, type preservation, bounded operation encoding, memory ordering, pointer provenance and imports/CFI. Optimization passes preserve observable behavior and emit sufficient evidence; unsupported proof class labelled testing-only. Backend supports deterministic native code and optional PGO/hot-cold code layout. Benchmarks compare to known compilers on fixed workload, not marketing claims.

## 5. Implementation priority

Host cross-compiler first under Ubuntu for reproducibility; later native self-hosting `.ivx` tools. Language core + deterministic CLI compile (AST, VSG, VBIR, IVO, IVX) precedes AI suggestions, huge standard library or automatic cross-target compiler. Avoid arbitrary architecture-specific cleverness until strict IR semantics and ABI freeze.

## 6. Acceptance

Two independent parsers for VSG/VBIR and IVX strict encodings, golden fixtures, type/effect/ownership negative tests, deterministic rebuild across machines, imported interface hash checks, AOT executable verified and started in ring3, separate `.ivl` and `.ivdbg` linking, crash resistance and performance measurements. Compiler error must reject ambiguity rather than hallucinate meaning.
