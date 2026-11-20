# IntellectOS C1 — ранняя консоль ядра через QEMU COM1

**Статус:** код для локальной приёмки; не hardware-verified. Исходная база: GitHub `quaxistech/IntellectOS` main `9367458e2e0f03f5e045a3683153714251643ea1` (Memory Core M1 D2). BootInfo ABI v1, BIOS/i386, UEFI Stage E bootloader **не меняются**.

## Назначение

C1 добавляет раннее зеркало framebuffer-печати через COM1 (начиная с валидного BootInfo) и базовую двустороннюю связь с ядром: диагностический интерфейс на виртуальном последовательном порту COM1 с фиксированным вводом, без зависимостей от UEFI Boot Services, файловой системы, IRQ, пользовательского режима, выделений из heap или терминальной графической подсистемы.

Это **не настоящий POSIX Shell, не Vibelang REPL и не реализация IVX Loader**. Команды встроены в ядро для диагностики готовности системных подсистем. При дальнейшей разработке полноценный командный интерпретатор переносится в ring3 и загружается как `.ivx`.

## Использование на Ubuntu 24.04

```bash
cd /home/quaxis/IntellectOS
# архив распакован отдельно, например в ~/Downloads/IntellectOS_C1_Console_Foundation
python3 ~/Downloads/IntellectOS_C1_Console_Foundation/tools/apply_c1.py --project "$PWD" --check
python3 ~/Downloads/IntellectOS_C1_Console_Foundation/tools/apply_c1.py --project "$PWD" --apply
make clean check
make uefi-console
```

В окне терминала запуска QEMU появится `intellect#`; введите `help`, `mem`, `vmm`, `boot`, `cpu`, `echo hello`, `halt`. Для просмотра экрана framebuffer в отдельном окне QEMU запустите `make uefi-console-gui`. Остановить QEMU: `Ctrl+C`. Никаких `/dev/sdX` или USB не требуется.

## Инварианты

- COM1 x86-64 16550-совместимый, базовый адрес 0x3F8, 38400 бод, 8N1, polling; нет IRQ и `sti`.
- UART absence: на framebuffer выводится `UART COM1 MISSING`, затем CPU штатно останавливается; не выполнять необоснованные записи на реальное оборудование.
- Максимальная длина команды 127 символов; ASCII printable only, ввод CR/LF, CRLF, backspace поддержаны. Ввод лишних байтов до следующей новой строки отбрасывается.
- Код не содержит исполнения произвольных адресов, редактирования PTE или записи на накопители.
- Инициализация COM1 выполняется после первичной проверки BootInfo, **до** PMM/VMM для зеркалирования начальных диагностик. Проверка PMM/VMM происходит **до** запуска интерактивного monitor; существующие memory gates должны успешно отработать.
- При раннем сбое до старта serial monitor QEMU с `-display none` может выглядеть зависшим: использовать `make uefi-console-gui` для framebuffer diagnostics.
- Ожидаемые тесты `make clean check`: все исходные тесты M1 плюс новые tests/test_console64.py. Дополнительно проверить на QEMU сам prompt и команды. Это ещё не доказательство работы на физическом Lenovo.
- Не делать `git add .` перед проверкой `git status --short` и `git diff --check`; не коммитить `build/`.

## Реализация

- `serial64.c`: bounded UART probe, tx/rx polling, no interrupts.
- `kmonitor64.c`: независимый от UART детерминированный parser; host harness подменяет output и memory stats.
- `kernel.c`: подключение C1 только после M1 PMM/VMM success.
- `run_uefi_console.sh`: запускает текущий UEFI image в QEMU/OVMF с изолированной VARS-копией.

## Следующие этапы

M2 — heap, memory hardening. Затем page-level permissions, full address spaces, ring3/syscalls/timer/scheduler; только после этого минимальный IVX Loader, VFS и ring3 shell. C1 не будет архитектурным препятствием: его parser можно удалить из kernel после появления полноценной shell/TTY службы.

## Дополнение к установке: существующий Memory Core M2

Если локальный проект уже содержит незакоммиченный `kheap64`, применяйте именно пакет `IntellectOS_C1_M2_Compatible.zip`. Его `tools/apply_c1.py` добавляет `CONSOLE64_OBJS` к уже существующей линковке, не удаляет `kheap64` из `MEMCORE64_OBJS`, не меняет тесты heap и размещает интерактивный монитор после всех существующих проверок успеха в `intellect64_main`. Старый пакет C1 несовместим с изменённым Makefile. Сначала используйте `--check`, только затем `--apply`.
