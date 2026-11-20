# IntellectOS U1 — первый настоящий терминал пользовательского режима

Статус: **реализация-кандидат, обязательна приёмка QEMU/Lenovo**. Пакет добавляется **после** подтверждённых M3-A, H1/H2, S0, L1 и B1. BootInfo ABI v1, BIOS Foundation 0.4, IVX-1 и IntellectFS не изменяются.

## Что именно делает ядро

1. Загрузочная UEFI-программа B1 выбирает Terminal Edition, завершает Boot Services; на ядро передаётся прежний BootInfo v1.
2. Ядро завершает PMM/VMM/M3/H1/H2/S0, рисует L1 welcome, затем вызывает `intellect_u1_terminal_run`.
3. Создаёт новый supervisor-owned PML4[1] (виртуальная база `0x0000008000000000`), три уровня таблиц, чистую отдельную страницу кода и страницу стека.
4. Проверенный на этапе сборки flat-образ терминала (`build/user/terminal.bin`, <= 4096 байт, entry RVA 0, нет релокаций) копируется в RX-страницу user space. Стек — отдельная RW+NX-страница, обе с U/S=1; всё ядро остаётся supervisor-only.
5. Устанавливаются 64-bit user CS=0x33, user SS=0x2B и TSS.RSP0 на отдельный kernel stack; IDT vector 0x80 доступен с DPL3. `iretq` запускает машинные инструкции процесса в CPL3.
6. Каждый `int $0x80` переходит к ядру; указатели и длины проверяются, вызовы ограничены `write`, `read_char`, `clear`, `info`, `exit`. Пользовательский код не получает MMIO/PIO или прямых прав на устройства.
7. При `exit` (либо исключении CPL3) ядро восстанавливает свой стек, удаляет user PML4 mapping, сбрасывает TLB, обнуляет и освобождает все 5 выделенных страниц. Далее доступен диагностический C1 как fallback.

## Терминал

- В framebuffer встроенный sink изолирован от C1; он отображает запросы пользовательской программы, поддерживает простой скроллинг и ASCII.
- `help`, `about`, `pwd`, `echo`, `mem`, `clear`, `exit` работают как команды *отдельной программы*, а не парсер ring0.
- Ввод через COM1 при его наличии и polled i8042 PS/2 scan-set-1. PS/2 в QEMU обычно работает при включённой стандартной клавиатуре. На Lenovo с клавиатурой только USB может понадобиться xHCI/HID — этот пакет **не обещает USB HID**.
- `mem` получает сведения только через syscall; `pwd` возвращает `/` в смысле bootstrap-виртуального cwd. Текущая ОС всё ещё не имеет реальной файловой системы, процессов со scheduler или учётных записей.
- Терминал **не является root-login**. Пароль root, авторизация и вход появятся в следующем установочном выпуске.

## Гарантии безопасности и ограничения

- Это **один статически встроенный** кодовый образ, а не обобщённый загрузчик файлов IVX. Формат `terminal.bin` — внутренний bootstrap-артефакт, его нельзя выдавать за `.ivx`. IVX-1 остаётся нормативной спецификацией для будущих пользовательских программ.
- Раздельные права страниц + проверки syscall предотвращают прямой доступ user-mode к kernel identity pages. `I/O bitmap` TSS запрещает user PIO при IOPL=0.
- Нет ASLR, scheduler, таймера, USB HID, DMA isolation, SMP, самостоятельной загрузки с IntellectFS, полноценных сигналов и подсистемы авторизации. Код user-mode не должен получать capability для разметки дисков.
- Интеграция пока использует глобальный CR3, но создаёт private user PML4 subtree в свободном PML4[1]. Это допустимо только для *одного процесса и отключённых IRQ*. Перед многозадачностью нужен отдельный CR3 на процесс и полноценный U2.
- При исключении из пользовательского режима ядро не должно паниковать: обработчик переводит управление на безопасный kernel resume. На ошибку ядра существующая аварийная диагностика остаётся фатальной.
- В тестах защищаются системные границы; ошибка `read`, неверный указатель, переполнение диапазона или запись по RX должны отклоняться. Для реальных попыток записи по RX/PIO в ring3 нужен отдельный отрицательный QEMU fault test.

## Сборка и gates

```
python3 tools/apply_u1.py --project /home/quaxis/IntellectOS --check
python3 tools/apply_u1.py --project /home/quaxis/IntellectOS --apply
cd /home/quaxis/IntellectOS
make clean check
make uefi-u1-smoke
make uefi-console-gui
make uefi-live-image
```

`make uefi-u1-smoke` проверяет действительный UEFI/OVMF → Ring3 → `int80` → ответ на `help`/`echo` → `exit` → возвращение в ядро. На Lenovo проверяется изображение и наличие ввода без утверждений об USB.

## Приёмочные условия

1. `make clean check`: все старые тесты BIOS, UEFI и M3/H1/H2/S0/L1/B1 + U1 проходят.
2. `terminal.bin` <= 4096B, entry RVA 0, нет ELF relocations/.got, собственный `bootstrap` не притворяется IVX.
3. QEMU: есть текст `U1: Ring3 user address space installed`, отдельный prompt `intellect:/$`, работают `help`, `echo U1_RING3_OK`, `mem`, `clear`, `exit`.
4. Отсутствуют дисковые записи, изменения BootInfo ABI и загрузочного меню.
5. При отсутствии клавиатуры система не объявляет её готовой. Вывод через framebuffer остаётся.

## Следующий обязательный функционал установочного выпуска

Native xHCI + USB HID (и при необходимости PS/2), Interrupt/APIC, IRQ-safe scheduler + process manager, IVX-1 loader/verifier, VFS + IntellectFS mount, UEFI/OS storage writes с crash-consistency, login/password/verifier, IANA timezone, installer GPT/FAT ESP/IFS и отдельная первая загрузка с HDD. Не заменять этот набор только экраном выбора раздела.
