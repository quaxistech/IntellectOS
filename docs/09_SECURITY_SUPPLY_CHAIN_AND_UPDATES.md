# IntellectOS — модель доверия, безопасность IVX, цепочка поставки и обновления

**Редакция:** 1.0 DRAFT · **Сфера:** boot trust, компилятор, источники `.vibe`, `.ivx/.ivl` подписанные образы, установленные библиотеки, AI-предложения, OS capabilities, защита от rollback. **Не подменяет** текущие ограничения Memory Core M1 и не заявляет, что Secure Boot уже интегрирован.

## 1. Принцип Zero Implicit Privilege

Никакой исходник, `.ivx`, подпись разработчика, ABI hash, запрошенный effect или AI-сообщение не является разрешением на административную операцию. Доверие разделено на: (a) **целостность** байтов, (b) **происхождение** по конкретной доверенной политике, (c) **совместимость** ISA/ABI/schema, (d) **авторизацию** на ресурсные операции, (e) **корректность поведения**, которая не выводится автоматически из первых четырёх свойств.

## 2. Угрозы

Входы противника: повреждённый USB/блочный носитель, подменённый `.ivx/.ivl`, malicious plugin, compromised package registry, слабые/отозванные release keys, ABI confusion, malicious VSG/VBIR/IVO, LLM prompt injection, недоверенный источник PGO-профиля, compromised build host, ошибочная firmware memory map, DMA от устройства, page fault race, TOCTOU между проверкой и отображением, межпроцессные timing side channels.

Активы: boot/kernel image, root key store, пользовательские данные, подписи, private signing keys, process address spaces, verified artifact store, installed closure state, trusted user consent state, audited build receipts.

## 3. Доверенные границы

```
Firmware/Boot trust (отдельный ранний этап)
     └── IntellectOS kernel TCB
          ├── trusted artifact + verified page mapping
          ├── capability/IPC syscall enforcement
          └── trusted policy/key/update services
                    ├── signed IVX and installed closure (проверенные данные)
                    ├── ordinary user apps (не TCB)
                    └── AI and compiler optimization proposals (не TCB)
```

Kernel не исполняет custom bytestring из манифеста как verifier code, loader script или разрешающий policy evaluator. `LoadPlan` принимается только с правами trusted service и проверяется по root/generation. Сервисный план — данные, не команда привилегированному интерпретатору общего назначения.

## 4. IVX signed-content и page integrity

В IVX-1 первые 128 байт — header с `content_root` по смещению `[96,128)`. Для вычисления root это поле считается нулями; остальные байты `[0,signed_region_len)` — неизменяемый canonical source. Hash = BLAKE3-256 leaf/node с domain separators, 4-КиБ leaf chunks и 16-арным Merkle. Дерево в хвосте, подпись следует за ним, подпись/дерево не входят в собственный root. Для 16/64-КиБ MMU страниц проверяются все затронутые 4-КиБ блоки до публикации соответствующего PTE.

Хеш-блок из Merkle **не** удостоверяет право на исполнение без signature policy, ISA, feature, segment permission, ABI и installation checks. Первую страницу с header нельзя дедуплицировать по «маскированному» leaf hash как по physical-page identity; фактические байты страницы должны совпасть. Shared RX/RO неизменяемы, private BIND/DATA/TLS инициализируются в staging и запечатываются до публикации.

**IVX Merkle ≠ Linux fs-verity format**: IntellectOS вправе реализовать собственную схему, но нельзя считать BLAKE3 root автоматически совместимым с чужой Linux версией fs-verity. Интеграция с host FS требует отдельного адаптера и независимых digest policies.

## 5. Подписи и ключи

`IVXS` envelope содержит version, bounded record count, algorithm IDs, key IDs, signature bytes. `signed_message = ASCII("IVX1/SIGN\0") || LE16(1) || content_root`. Поддержка Ed25519 предполагается базовым профилем, ML-DSA-65 (FIPS 204) — для повышенной политики, включая dual signing при выбранном системном trust rule. Требование двух подписей ко всем крошечным приложениям не является обязательным: это увеличивает размер и ресурсы. Неизвестные обязательные алгоритмы, пустая подпись или неизвестный ключ — отказ.

KID не говорит сам, что ключ доверен: TrustStore хранит roots, области действия, expiration/revocation и installer-side policy. Development key может разрешать только developer domain без административных прав. Release signing оформляется отдельным сервисом с ограниченными полномочиями; модель AI не имеет ключей.

## 6. Boot Security / recovery

На текущем этапе UEFI loader unsigned, поэтому Secure Boot machine policy может отвергнуть `BOOTX64.EFI`. Достижение production chain требует либо собственной доказуемой цепи trust от firmware, либо подписанного/измеренного loader по выбранной политике и отдельного восстановления. Восстановление boot/kernel, проверка kernel image и IVX user signing — разные уровни и разные key scopes. BootInfo v1 не становится signed IVX от упоминания в документации.

Восстановление должно иметь bounded rescue state с проверенным read-only system set, диагностикой без AI, независимым recovery authorization и rollback protection. Внедрение anti-rollback без рабочего recovery plan может привести к полной неработоспособности ПК после ошибочного обновления.

## 7. Capability policy

Системное решение — пересечение `requested_in_ivx ∩ publisher_scope ∩ user/admin_grant ∩ process_namespace ∩ current_revocations`. Итог — конкретные ограниченные handles. Передача handle в IPC требует explicit transfer rights и никогда не повышает scope. Неявные ambient privileges (общий root к файловой системе по string path, глобальный socket namespace без rights, скрытый LD_PRELOAD) запрещены.

Права могут быть отозваны для последующих операций; уже переданные данные не «отзываются назад» магически. Для shared memory и long-lived device handles нужен revocation protocol и bounded reference lifetime.

## 8. Пользовательский executable и memory defenses

- User-mode ring3, непроницаемый kernel/user page-table boundary.
- W^X включая все aliases; user stack NX, guard pages.
- ASLR для image/library/stack/heap с реальным энтропийным бюджетом.
- CET/IBT/SHSTK, BTI/PAC/MTE включаются только на поддерживающих процессорах и по точному ABI.
- RELRO: BIND seal before execution; no always-writable `.got.plt`.
- RX/RO shared pages остаются бит-в-бит одинаковыми при разных ASLR slides.
- Живые указатели и аппаратные CHERI/PAC values создаются в runtime, не сериализуются как «готовые полномочия» в `.ivx`.
- Pure `.ivl` code в общем адресном пространстве не обеспечивает sandbox; для недоверенных plugins — изолированный IPC компонент.

## 9. Безопасность парсеров

Любой файл формата считается недоверенным до полной проверки: `u64` offsets/sizes с `u128` промежуточной проверкой; единственная интерпретация; bounded counts; unknown mandatory flags reject; no overlapping executable and writable virtual pages; no recursive parser on page fault; no text relocations в immutable TEXT. VSG/ VBIR/ IVO/ PKG reader не наследует доверие IVX только из общего производителя. Никаких bytecode fixup scripts, RPATH/LD_LIBRARY_PATH, пользовательского IFUNC, unbounded ZSTD stream для исполняемого image.

## 10. Supply chain проекта

### 10.1. Исходники и зависимости

`vibe.project.toml` фиксирует логические requirements; `vibe.lock` содержит exact source content digests; скачанные artifacts проверяются по trust policy/контентному хешу. Экспериментальные registry packages не могут поставлять произвольный build script, который выполнится автоматически с правами хоста.

### 10.2. Сборка

Build host toolchain digest, deterministic source order, explicit target, reproducible unsigned IVX content, full build receipt and binary provenance. AI proposals не могут читать секретные ключи или отправлять private project content во внешний inference endpoint без разрешения. Профили PGO из недоверенных источников не позволяют отключить compiler security passes.

### 10.3. Подпись и дистрибуция

IVX подписывается авторизованным builder; optional package signature связывает дополнительные ресурсы/ISA variants; developer permissions и publisher identity не дают OS admin автоматически. SBOM в формате SPDX/CycloneDX содержит точные build artifact roots, а не только человекочитаемые имена.

## 11. Атомарная установка и update closure

1. Проверить сам `.ivpkg` и bounded chunk manifest.
2. Для каждого `.ivx/.ivl` проверить полную IVX подпись/content Merkle и canonical tables.
3. Проверить exact ABI, interface types, platform features, namespace policy, caps.
4. Материализовать dependencies into `ClosureRecord` с точными content digests и import RVAs.
5. Сгенерировать проверенный `LoadPlanV1` и необходимые installation hints.
6. `fsync` immutable blobs + journal/transaction state до active switch согласно выбранной FS durability модели.
7. Атомарно опубликовать `generation N+1` для новых spawn, сохранив `generation N` для действующих процессов.
8. Cleanup только после ухода всех holders и после expiry rollback retention.

Нельзя считать UPDATE атомарным, если файл библиотеки изменился in-place при сохранении старого digest. При crash в любой промежуточной фазе должен восстанавливаться либо старый полноценный closure, либо новый полноценный, но не смесь.

## 12. Отзыв, компрометация и rollback

Ключи могут отзываться по policy generation; при доступе к обновлённому revocation data новые запуски подозрительного image отклоняются. Уже работающий executable может быть остановлен по выбранной безопасности политике, но это явное решение, не молчаливый патч его RX. Anti-rollback привязан к policy epoch/monotonic trust counter, а не к системным часам, которые можно изменить. Recovery/controlled downgrade требует отдельного полномочия и журнала.

## 13. AI-specific threat model

Prompt injection из файла/страницы/лога не может стать sysadmin instruction. Модель получает ограниченный контекст и возвращает typed proposal, который проходит deterministic schema checker, resource policy, approved tool list, required user consent и action receipts. Объяснение ИИ не может превратить `UNVERIFIED` в `VERIFIED`, назначить право `kernel.write` или заменить content root при signature mismatch.

## 14. Инциденты и аудит

Стандартные события: `EXEC_DENIED_BAD_SIGNATURE`, `EXEC_DENIED_ABI`, `IVX_PAGE_INTEGRITY`, `CAPABILITY_DENIED`, `HANDLE_STALE`, `INSTALL_ROLLBACK`, `KEY_REVOKED`, `BUILD_REPRO_FAILURE`, `AI_PROPOSAL_REJECTED`. Записываются timestamps monotonic+wall estimates (не использовать wall clock для ключевой безопасности без time trust), artifact root, process principal, policy version, technical reason. Не логировать private key, пользовательские пароли, содержимое файлов и промптов по умолчанию.

## 15. Приёмка

- Мутировать каждый байт подписанного metadata region → signature/root rejection для соответствующего файла.
- Мутировать область, не прочитанную при запуске, затем page-in → integrity failure, без RX.
- Закрыть/передать handle, попытаться применить старый ID/generation → отказ.
- Подменить `.ivl` другим хешем без смены closure → STALE_CLOSURE отказ.
- Crash в каждом состоянии install и recovery → целый старый/новый closure.
- Циклические/двойные fixups, переполнение offsets, decompression bombs → bounded rejection.
- Model outage/injection → ядро/права и базовый запуск unaffected.
- Подписанный, но запрашивающий недопустимые права image → не получает эти права.

**Статус:** model/threat contract для будущей реализации. Пока нет user mode и IVX loader, перечисленные проверки не были выполнены на IntellectOS.
