# Реестр архитектурных решений, противоречий и устаревших требований

**Обязательное правило:** одно решение — один текущий владелец, старые документы только research/history. Статус `DECIDED` не равно `IMPLEMENTED`.

| ID | Старое/конфликтующее состояние | Принятое решение | Проверка / источник |
|---|---|---|---|
| AD-01 | отдельные Branch/дубли проектных каталогов | единый `/home/quaxis/IntellectOS`, работа в `main` | 18; git |
| AD-02 | картинки/меню проектировались на 1920×1080 | **адаптивные размеры по GOP width/height/pitch**, proportional UI/fallback | 09; framebuffer tests |
| AD-03 | Lenovo как implicit hardware target | Lenovo — disposable physical test, продукт для **разных поддерживаемых** компьютеров | 04; multi-vendor gates |
| AD-04 | `root` password min 12 UTF-8 | **0..256 bytes**, любые короткие/пустые, явное warning и scoped authentication | 08; negative login tests |
| AD-05 | первый установщик обозначен как вся ОС | real install = GPT + ESP + genuine IFS + verified IVX + init/login + boot after USB removal | 08,14 |
| AD-06 | U1 `terminal.bin` называли `.ivx` без реальной загрузки | U1 bootstrap отличен от signed IVX file path; developer pin only limited profile | 07 |
| AD-07 | IFS boot format называли полной RW FS | ограниченный sealed boot profile ≠ CoW RW/crash recovery | 06,19 |
| AD-08 | UEFI Block IO считали native драйвером | firmware protocols доступны только до ExitBootServices; native AHCI/NVMe/USB отдельны | 03,04 |
| AD-09 | BIOS и UEFI как два конкурирующих продукта | два firmware adapters, общее целевое OS architecture; i386 BIOS текущий diagnostic fallback | 03 |
| AD-10 | терминал vs графическая редакция | console/base/server first; desktop future userland на общем kernel ABI | 01,02,09 |
| AD-11 | красивый boot экран заблокирует загрузку | эффекты optional/nonblocking, quiet/debug/headless fallbacks | 09 |
| AD-12 | звук при загрузке «на любом ПК» | только при доступном native audio backend, можно mute, без него boot нормально | 04,09 |
| AD-13 | AI должен «управлять всем» в kernel | AI userland advice, capability checks, no model on correctness path | 02,11,12 |
| AD-14 | Vibelang как исполняемые prompts | детерминированный typed compiled language, AI optional | 10,11 |
| AD-15 | IVX как новая virtual machine / новый язык | IVX signed **native executable format**, Vibelang отдельно | 07,10 |
| AD-16 | неподтверждённые performance победы/опции | benchmark first; advanced policy optional behind feature flags | 06,07,14 |
| AD-17 | signed manifest automatically grants privilege | OS policy/capabilities determine permissions, signature confirms identity | 07,12 |
| AD-18 | вся история M1/C1/B1/I2 как отдельные текущие ТЗ | stage reports history only; new canonical docs describe current goal and actual code | 00,15 |
| AD-19 | v1 означает frozen schema | V1 draft, boot profile, frozen distinct; test vectors required | 19 |
| AD-20 | root auth host `provision()` = работающий kernel login | actual runtime login/session separate requirement | 08,15 |
| AD-21 | host image installed = physical installed OS | 2-stage QEMU + real controlled hardware boot required | 14 |
| AD-22 | полный GUI обязателен до terminal install | adaptive firmware installer UI allowed, runtime compositor later | 08,09 |
| AD-23 | все драйверы с нуля ради чистоты | собственные драйверные границы; допустимо изучение/адаптация проверенных permissively licensed источников при provenance/ABI tests | 04,12 |
| AD-24 | публичный GitHub плюс ключи/пароли в сборке | исходники/документы и публичные fixture допустимы; реальные secrets не commit | 12,18 |
| AD-25 | новые документы пишутся поверх устаревших без контроля | `docs/` root canonical, `specs/` полные форматы, `research/` старые концепции, `history/` milestone reports | 00 |

## Оставшиеся открытые проектные решения (не считать «забытыми»)

- Binary freeze полного IVX manifest/ABI/import/export/fixup/signature policy, exact numeric registries, trust roots.
- Полный IFS on-disk multi-shard, allocation, snapshots, encryption, GC and power-cut semantics and stable GUID.
- User ABI syscall numbers beyond U1, full process manager and `init` service contract.
- Root auth implementation location (kernel/user service) и policy authenticated recovery; empty local password must not permit network/remote login by implication.
- Secure Boot trust-chain enrollment, rollback monotonic anchor and key revocation.
- Native USB xHCI/NVMe/AHCI/AUDIO and supported hardware matrix; headless boot without GOP.
- Detailed graphical desktop/compositor/driver API; SLOs and accessibility policy.
- Name of AI compiler product; advanced ISA and IVX optimization profiles after benchmarks.

Open decisions входят в `19_FORMAT_FREEZE_REGISTER`/roadmap; они **не повод постоянно тормозить** разработку: реализовывать явно ограниченный boot/alpha profile и честно отмечать область готовности. Нельзя объявить incomplete schema production frozen без fixtures.
