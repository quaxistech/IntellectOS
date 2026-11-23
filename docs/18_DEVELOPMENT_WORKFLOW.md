# Совместная разработка, GitHub и управление документацией

## 1. Один репозиторий

Локальная база: `/home/quaxis/IntellectOS`; удалённая: `https://github.com/quaxistech/IntellectOS.git`. Все интеграции предназначены для **`main`**. Не создавать отдельные `IntellectOS-debug`, параллельные копии под тем же именем, не менять ветку без явной причины. Test images, generated headers и packages — внутри `build/`, который исключён из version control.

Сохранение в Git — на существующей машине пользователя (код в ChatGPT/агенте не равен файлам локального ПК). Проверки перед commit: `git status -sb`, `git diff --check` для кода (Markdown trailing double spaces допустимы), `make clean check`, focused QEMU smoke, scan staged names **и содержание** секретов, then one coherent commit and `git push origin main`. Документационная операция не должна молча писать на аппаратные диски.

## 2. Как передавать изменения

Для множества файлов — единый versioned archive with file paths relative to project root + noninteractive installer script `--check/--apply/--verify`, reproducible changelog, SHA-256 manifest, no hidden `curl | bash`. На машине всегда использовать действительный код `main`; старые ZIP-патчи не применять подряд без проверки. Пакеты, которые только создают образ, нельзя называть полноценным установленным OS release.

Изменять проект атомарно: backup changed sources; check expected anchors/hashes; stop on unknown shape rather than corrupt `kernel.c`; no generic string replace across code without tests; idempotent patcher; no unbounded destructive write. Re-run tests after patch and preserve logs.

## 3. Документация

- Сначала выбрать владеющий документ 01–19 либо `specs/`. Один факт — одно актуальное место.
- Архитектуру, форматные offsets и runtime-статус **не смешивать**; изменение бинарного ABI требует freeze registry, fixtures and compatibility analysis.
- Старое содержимое после реорганизации находится в `docs/research/` и `docs/history/`, не является альтернативным ТЗ. Две полные спецификации сохраняются в `docs/specs/`.
- Каждое архитектурное изменение обновляет 16 (решение), 17 (requirements), 13 (milestone) и affected spec; каждая реализация — 15 (current verified state) и 14 (test evidence).
- Не создавать `v2` и фиктивные миграции пока первый wire protocol не стал FROZEN; исправления редактируют v1 напрямую, в Git сохраняется history.
- Не заменять сотни страниц обязательных таблиц одной краткой заметкой; первичные подробные записи сохраняются целиком.

## 4. Автоматическая проверка

Проводить sanity check Markdown links, relative paths, no broken links to current docs, proper status labels, unique requirement IDs, no contradictory `CURRENT` claims, no hard-coded display dimensions as supported-only targets, no hard password minimum, no cleartext secrets, file/path casing. Verify and report actual `git diff --stat`. При изменении документации run static check; не требовать compilation ОС, если менялись только Markdown.

## 5. Лицензирование и зависимости

Использовать собственные файлы или лицензии с проверенными условиями распространения. При заимствовании драйверов/шрифтов и звуков — LICENSE, attribution, upstream commit, security/portability audit. Собственный репозиторий публичен, но публикация кода не является автоматической лицензией стороннего ресурса. Не коммитить fonts from container tooling or чужие copyrighted assets без оснований.

## 6. Hardware safety

Все destructive test operations первоначально с regular image files/QEMU; физическая запись только после ручной идентификации устройства и назначения, отдельного подтверждения и backup. Никакие `dd` на `/dev/sdX`, `wipefs`, partition editors в документационных установщиках. Инженерная помощь не может утверждать remote GitHub write access при отказе API `403`.

## 7. Как отвечать на запрос «до конца»

Формулировать критерий завершения, предоставлять единый интегрируемый код, конкретные команды, реальные результаты тестов и честные blockers. Не объявлять один элемент из четырёх готовой ОС. Не требовать повторного подтверждения уже выбранных правил (пустой пароль, main, hardware agnostic, console-first, own IntellectFS/IVX).
