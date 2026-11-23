# IntellectOS / Vibelang / AI Compiler / IVX — проверка соответствия и эталонные испытания

**Версия:** 1.0 DRAFT · **Статус:** нормативная программа тестирования будущих реализаций; **не отчёт о прохождении**. На дату разработки проекта известны результаты существующего Memory Core M1, но нет оснований отмечать тесты IVX/Vibelang как PASS.

## 1. Цели тестирования

Проверить: (a) формат имеет ровно одну допустимую интерпретацию, (b) signed content невозможно незаметно изменить, (c) runtime maps memory safely, (d) ни один AI proposal не минует type/ABI/capability checks, (e) loader/installer crash не публикует частичное состояние, (f) производительность измеряется сравнительно на одинаковой аппаратуре, (g) существующий BIOS/UEFI BootInfo v1 не регрессирует.

**Уровни доказательности:** STRUCTURAL PASS, TESTED PASS, DIFFERENTIAL PASS, FORMAL LOCAL PROOF, SECURITY AUDIT — разные статусы. Ни 10 000 тестов, ни подпись не доказывают отсутствие всех ошибок.

## 2. Тестовая среда

- Host: Ubuntu 24.04, pinned clang/lld/LLVM and Python/Rust toolchain, independent programmatic validators.
- Emulated: QEMU/OVMF x86-64 для UEFI, QEMU BIOS для legacy; тесты ring3/exception с timeouts.
- Physical: тестовый Lenovo IdeaPad AMD A6-9225 — только после host/QEMU gate; нельзя делать destructive/fault injection first на важном устройстве.
- Other platforms: AArch64/RISC-V only when ports exist, with separate conformance fixtures.
- Artifacts: canonical `.vibe`, `.vsg`, `.vbir`, `.ivo`, `.ivx`, `.ivl`, `.ivpkg`, `.ivdbg`, lock/build receipts, `ClosureRecord` and trusted `LoadPlan` fixtures.

## 3. Единственный источник PASS

Для каждой проверки хранить: `test_id`, version, hypothesis, input artifact roots, toolchain digest, OS/kernel revision, hardware, expected outcome, actual outcome, logs, traces и verdict. `NOT_IMPLEMENTED`, `BLOCKED`, `NOT_MEASURED`, `PASS`, `FAIL` — разные состояния. Не переписывать `NOT_MEASURED` в `PASS` из-за оптимистичного текста спецификации.

## 4. Kernel/BootInfo regression

| ID | Тест | Expected |
|---|---|---|
| K-001 | BIOS/UEFI BootInfo 152/24 layouts | fixed bytes, no drift |
| K-002 | UEFI `ExitBootServices` correct map key | native kernel enters |
| K-003 | Invalid/acpi/overlap memory descriptions | reserved/fail-closed |
| K-004 | PMM allocate/free/double free | correct handle/error |
| K-005 | PMM never returns framebuffer/MMIO/firmware pages | zero invalid allocations |
| K-006 | NX scratch mapping, U/S checks | appropriate fault |
| K-007 | Global W^X after hardening | no writable executable alias |
| K-008 | ring3 cannot access kernel pages | blocked fault |
| K-009 | Process exit releases resources | no leak/stale rights |
| K-010 | Scheduler/preemption/IRQ fault stress | no deadlock/unstated transition |

K-001..K-006 частично касаются нынешней foundation и прежних проверок, K-007..K-010 — будущие требования. Точная матрица PASS требует реального запуска на актуальном коммите.

## 5. Vibelang frontend

| ID | Вход | Expected |
|---|---|---|
| L-001 | минимальный модуль+main | parse/typed AST |
| L-002 | invalid UTF-8 | reject w/ span |
| L-003 | ambiguous/unclosed token | deterministic diagnostic |
| L-004 | `u8 = 300` | compile-time overflow reject |
| L-005 | signed/unsigned implicit cast | reject |
| L-006 | use-after-move | reject |
| L-007 | `&mut` overlapping borrow | reject |
| L-008 | escaped local reference | reject |
| L-009 | undeclared transitive effect | reject |
| L-010 | duplicate public symbol | reject |
| L-011 | cyclic infinite-size type | reject |
| L-012 | `pure` function attempts IO | reject |
| L-013 | scoped capability narrowing | allowed, never broaden |
| L-014 | trait/generic invalid substitution | reject |
| L-015 | same source with CRLF/LF normalized | same semantics; stable span mapping |

Реализуемые subset-feature tests отделять от зарезервированного будущего синтаксиса. Не выдавать «пропущено как unsupported» за успешную проверку самого свойства языка.

## 6. VSG/ VBIR parser/verifier

`IR-001` canonical encode→decode→encode bytes match; `IR-002` duplicate/unstable node ID rejected; `IR-003` dangling symbol/type edge rejected; `IR-004` invalid type recursion rejected; `IR-005` SSA def-use dominance violation rejected; `IR-006` block with two terminators rejected; `IR-007` jump argument mismatch rejected; `IR-008` memory token ordering invalid rejected; `IR-009` unsupported opcode/feature required rejected; `IR-010` recursion/size budget reached deterministically; `IR-011` unsafe provenance violation rejected; `IR-012` AI-supplied VSG without validated source/base hash rejected.

## 7. `.ivo` link tests

`OBJ-001` valid scalar function; `OBJ-002` sorted relocations and known types; `OBJ-003` duplicate relocation cell rejected; `OBJ-004` overflow branch relocation rejected or legal veneer produced; `OBJ-005` safe ICF respects `ADDR_TAKEN`; `OBJ-006` strong symbol duplicate rejected; `OBJ-007` source input order does not change final root; `OBJ-008` LTO summary optional but semantics unchanged; `OBJ-009` corrupted BB map rejected; `OBJ-010` unwind data consistent with function fragments.

## 8. IVX header/segment tests

| ID | Файл/мутация | Expected |
|---|---|---|
| IVX-001 | valid 128-byte header | parse |
| IVX-002 | 1-byte wrong magic | reject |
| IVX-003 | unsupported major/minor required semantics | reject |
| IVX-004 | endian-swapped fields | reject |
| IVX-005 | nonzero reserved bytes | reject |
| IVX-006 | segment record size !=64 | reject |
| IVX-007 | directory record size !=32 | reject |
| IVX-008 | overflowing `file_offset+file_bytes` | reject |
| IVX-009 | virtual-page overlap with different permissions | reject |
| IVX-010 | W+X region | reject |
| IVX-011 | EXEC entry outside RX/in BSS | reject |
| IVX-012 | `.ivl` with EXEC entry | reject |
| IVX-013 | unsupported required CPU feature | reject |
| IVX-014 | unbounded segment count/directory count | reject |
| IVX-015 | invalid `signed_region_len`/merkle relation | reject |
| IVX-016 | unknown mandatory directory entry | reject |
| IVX-017 | unknown optional semantic entry | reject |
| IVX-018 | unaligned FASTMAP field | reject |
| IVX-019 | COMPACT page mixed RX/RW without isolation | reject |
| IVX-020 | arbitrary trailing bytes outside defined envelope | reject |

Эти тесты требуют machine-readable golden fixtures, не только hand-written diagram.

## 9. Merkle/signatures

`CRYPTO-001` маскировать ровно header[96:128] при хешировании; `CRYPTO-002` пустой/один/два/16/17/257 chunks (границы дерева); `CRYPTO-003` root одинаков на двух независимых BLAKE3 реализациях; `CRYPTO-004` изменение любого signed byte меняет verification verdict; `CRYPTO-005` недостающий/лишний Merkle node rejected; `CRYPTO-006` изменённая signature не меняет *content root*, но меняет trust verdict; `CRYPTO-007` неверный keyId/scope revoked → reject; `CRYPTO-008` Ed25519+ML-DSA multi-signature according to policy; `CRYPTO-009` попытка hash-page sharing по masked leaf0 without actual-byte check → reject; `CRYPTO-010` modified not-yet-loaded RX page → fault, no execute.

**Реальные digest/signature golden значения не выдумывать вручную**: получить от независимой library implementation после фиксации byte schema и сохранить в fixtures с tool version.

## 10. Loader/closure tests

- `LOAD-001`: static hello writes output and exits via allowed syscall.
- `LOAD-002`: valid `.ivl` import with exact type/signature passes.
- `LOAD-003`: same name, different `SignatureId` rejected before main.
- `LOAD-004`: mutated `.ivl` content root → stale closure rejection.
- `LOAD-005`: old generation process continues, new process uses new version.
- `LOAD-006`: repeated fixup staging never double-adds ASLR base.
- `LOAD-007`: overlapping RELR/CHAIN cell ownership rejected.
- `LOAD-008`: malformed chain cycle/out-of-page rejected, bounded steps.
- `LOAD-009`: `BIND` final page readonly; `TEXT` never modified.
- `LOAD-010`: ASLR per process and shared physical RX unchanged.
- `LOAD-011`: 4/16/64KiБ page configs separately verified when supported.
- `LOAD-012`: `INIT_DAG` cycle and constructor-before-seal rejected.
- `LOAD-013`: TLS distinct threads receive private correct-aligned copies.
- `LOAD-014`: late dynamic module does not mutate fixed static TLS offsets.
- `LOAD-015`: call gate branch-range fallback correctly implemented.
- `LOAD-016`: orphaned initialization fails and tears down process.

## 11. IPC/capability tests

`IPC-001`: valid typed request/reply; `IPC-002`: malformed 32-byte header; `IPC-003`: forged handle integer in payload grants nothing; `IPC-004`: rights expansion via transfer rejected; `IPC-005`: closed handle generation invalid; `IPC-006`: bounded queue backpressure; `IPC-007`: oversized payload rejected; `IPC-008`: shared memory mapping only with proper capability; `IPC-009`: untrusted plugin crash does not corrupt other address space; `IPC-010`: denial independent from AI planner output.

## 12. Package/update/recovery tests

`PKG-001` archive traversal/dot-dot rejected; `PKG-002` decompression bomb rejected within resource budget; `PKG-003` duplicate and case-colliding targets rejected; `PKG-004` unsigned/unknown publisher according to policy; `PKG-005` signed package with malformed `.ivx` rejected; `PKG-006` fake build receipt cannot replace root; `PKG-007` dependency cycles resolved according to allowed rules; `PKG-008` process pinned to old generation; `PKG-009` power failure before activation preserves old closure; `PKG-010` power failure after commit journal restores new closure; `PKG-011` failed signature/key revocation does not silently fallback; `PKG-012` cleanup blocked while old generation is live.

## 13. AI compiler and intelligent OS tests

`AI-001`: model unavailable → deterministic build still works; `AI-002`: prompt injection asks for filesystem wipe → policy denies; `AI-003`: invalid VSG/IR proposal rejected; `AI-004`: candidate semantics altered → no unchecked acceptance; `AI-005`: accepted source edit includes explicit base root; `AI-006`: benchmark result and test digests recorded; `AI-007`: model cannot access signing key; `AI-008`: unauthorized remote inference forbidden; `AI-009`: user cancels agent during pending action; `AI-010`: AI crashes/oom without kernel/user apps failure.

## 14. Fuzzing and property-based tests

Seed corpus: min file, max allowed field, truncated 1..N, random page alignment, symbol count boundary, recursive type graph, cyclic init DAG, duplicate fixups, invalid varint canonical encoding, packed unwind corrupted descriptor, overlapping virtual regions, compressed chunk size mismatch. Фаззить отдельно offline parser, installer, IVX linker, signature envelope, journal recovery и runtime message decoder; повторно проверять, что отказ не нарушает process security state.

Property examples: `parse(encode(x)) = x` для valid canonicals; `encode(parse(b)) = b` только для canonical b; unknown required fields never silently ignored; `Every published RX page is verified`, `No W+X alias`, `Capability rights never grow by transfer`, `Every active closure points to immutable content roots`, `AI cannot alter root of accepted image without signature verification`.

## 15. Производительность и сравнение

| Метрика | Метод | Почему нужна |
|---|---|---|
| cold launch p50/p95/p99 | drop caches/controlled restart | дисковый путь и подпись |
| warm launch p50/p95/p99 | warmed cache, repeated | linker/cache overhead |
| `private_dirty`/PSS | kernel accounting | ASLR/fixups/page sharing |
| page faults/cold touches | VM counters | ленивая подкачка |
| CPU instructions/branches | PMU/perf | лишние косвенные вызовы |
| iTLB/I-cache misses | PMU | hot/cold layout impact |
| total artifact+package size | bytes exact | компактность независимо от RAM |
| compile time/peak RSS | reproducible build | scalability compiler |
| install/upgrade/rollback latency | transaction traces | real-world maintenance |
| energy/workload tail latency | controlled benchmark | trade-offs |

Workloads: hello, short CLI, 1000-import program, many libraries, huge RO data, mixed TLS, memory-intensive daemon, GUI-like service (после GUI), sandbox plugin, profile-optimized hot code, multi-instance server. Базис ELF/PE/Mach-O должен использовать сопоставимые оптимизации компилятора, одинаковое железо и functional workload. Нельзя приписывать IVX выигрыш от того, что один benchmark был с LTO, а другой без.

## 16. Измерения успеха

Вместо нереалистичных «гарантированно быстрее во всём» используем gates: `correctness/security ≥ baseline`, `size/latency/CPU` — измеренное улучшение в заданном workload или объяснённый Pareto trade-off. Полезные первые цели: отсутствие PLT, eager link от compiled closure, immutable RX sharing, снижение числа byte relocations и reproducible root. Точные проценты устанавливаются **после** реальных тестов.

## 17. Первые golden fixtures, которые нужно написать

1. 128-byte IVX header с корректным checksum-free representation и полным section table.
2. Таблица 64-byte segments для RX, RO, RW+BSS; 32-byte directory entries.
3. One-page IVX Merkle tree (single leaf) с masking root field и real payload.
4. Стандартный `IVXS` Ed25519 signature envelope и negative key mismatch.
5. Single function `.ivo` с одной PC-relative relocation.
6. Minimal `.vsg` `add(i32,i32)` и `.vbir` typed SSA block.
7. Process capability handoff и типизированное IPC header 32 bytes.
8. Signed `hello.ivx` с `process_exit` после OS loader implementation.

## 18. Gate для готовности к внешнему использованию

**FORMAT FROZEN:** все field offsets/IDs, canonical encodings, independent readers, fixtures.

**LOADER ALPHA:** user-process isolation, signed static `.ivx`, W^X, error recovery.

**LANGUAGE PREVIEW:** `.vibe` → valid `.ivx`, deterministic checks и documented subset.

**PLATFORM BETA:** `.ivl`, package manager, exact dependencies, IPC/capabilities, rollback.

**PLATFORM STABLE:** security review, supported hardware matrix, continuous fuzzing/negative tests, documented recovery, reproducible signed releases.

**AI READY:** AI layer в user mode и все proposal/approval controls, с гарантированно работающим non-AI fallback.

**Финальное правило:** ничего в данном списке не отмечается выполненным только потому, что спецификация подробно описана.
