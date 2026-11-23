# IntellectOS `main` — наблюдаемое состояние на 2026-11-23

**Область:** статический аудит GitHub `quaxistech/IntellectOS/main`, snapshot tree `aab7414c8a5b4e4d2a3223e68961b01ca58a5916`, 237 Git tree entries (33 старых Markdown-документа). Эта таблица не является результатом локального `make` после этой даты. Проект мог измениться — перепроверяйте hash.

## 1. Что подтверждено исходниками

| Подсистема | Наличие исходников | Уровень факта/ограничение |
|---|---|---|
| Legacy BIOS MBR+Stage1/i386 | `boot/bios`, `kernel/i386`, `kernel/src` | отдельный diagnostic boot path |
| UEFI Stage E и BootInfo | `boot/uefi`, `kernel/include/bootinfo.h` | EFI/BootInfo 152B, native x86-64; совместимость ограничена драйверами/firmware |
| Memory Core PMM/VMM/M3 | `kernel/src64/{pmm64,vmm64,m3protect64,...}` | стартовая защита, не fully general SMP |
| H1/H2 diagnostics | `hardware64`, `driver64`, `hwdiag64` | discovery+начальные adapters; inventory != drivers |
| S0 block | `ata_pio64`, `block64`, `partition64` | Intel PIIX IDE primary master readonly, LBA28, 512B; не native NVMe/AHCI/USB |
| U1 Ring3 terminal | `userterminal64`, `usergate64.S`, `user/terminal.c` | одиночный пользовательский процесс, `int 0x80`; свидетельство QEMU U1 ранее от пользователя |
| VFS read ESP | `fat32_vfs64.c` | GPT/FAT32 read-only, bounded 8.3 file lookup; не универсальный VFS RW |
| IFS initial profile | `installer/ifs_disk_v1.py`, `ifs_ro64.c`, `ifs_blake3_v1.c` | image formatter/reader проверенного sealed bootstrap, не полный durable RW |
| IVX developer boot | `installer/ivx_v1.py`, `ifs_exec64.c`, `make_pinned_dev_ivx.py` | pinned one-file developer `.ivx`, не generic trust-store loader |
| Host disk install | `installer/target_disk_installer.py`, `tools/install_target_disk.py` | GPT/FAT32/IFS image builder and restrictive device safety, not a complete interactive partition editor |
| USB-to-disk QEMU | `tools/test_usb_to_disk_install_qemu.py` | test **script exists**; PASS only if independently run on current commit |
| UEFI boot menu/install | `boot/uefi/bootmenu.c` | contemporary high-resolution preboot UI with native Block I/O writes; must review physical safety independently |
| `root` data/timezone | `installer/system_config_v1.py`, `tools/provision_root_config.py` | host config with Argon2id; not equivalent to implemented kernel login |
| Graphics | `kernel/src64/terminalfb64.c`, `kernel/include/font8x16.h`, UEFI GOP | text/UI; user images are local `media/img`, not auto-included in repo/EFI |
| Audio/network/modern native USB | no corroborating driver modules in snapshot | not claim implemented |
| Vibelang/AI compiler/runtime | detailed docs; no complete compiler/runtime sources in snapshot | project goal, not running functionality |

## 2. Неполная согласованность кода/политики

- `tools/provision_root_config.py` на GitHub snapshot всё ещё запрашивает `root password (12+ UTF-8 bytes)`; это **противоречит** принятому требованию optional password. `installer/system_config_v1.py` уже обновлён в другом контексте, но синхронность callsites нужно проверить сборкой; источник истины решения — 08/16, не устаревший prompt.
- `boot/uefi/bootmenu.c` описывает full installer и может писать диск до EBS. Его capability-счётчик не доказывает native storage после EBS; метод выбора target и readback требуют отдельного audit.
- `kernel/src64/ifs_exec64.c` сам прямо говорит, что pinned developer IVX loader не является general in-kernel Ed25519 verified loader.
- `README.md` в корне содержит исторический Stage E checkpoint и может ошибочно утверждать, что нет файловой системы/пользовательских программ. Он остаётся исходным рабочим README; при применении новой документации необходимо отдельно заменить его short landing page **после проверки текущего кода**.
- `docs/INSTALLATION_FOUNDATION_I2.md`, `docs/USER_TERMINAL_U1.md`, `docs/BOOT_MENU_B1.md`, старые `ROADMAP/MILESTONES` датированы прежним состоянием и перемещены в history — не используются для статуса сегодня.
- Название `IntellectFS_v1` не означает проверенную стойкость к потере питания и full writer.
- Пример `RootPass123!` в target `Makefile` является только test fixture; разработка не должна навязывать эту строку пользователям.

## 3. Что подтверждал пользователь в текущем разговоре

- Ring3 U1 пользовательский терминал реально печатает команды в QEMU, работает и возвращает в ядро.
- Последний присланный `make clean check` после IFS-патча дошёл до 64 тестов: 63 PASS, один ERROR из-за отсутствующей Python библиотеки Argon2. Позже пользователь сообщил **68 passed и U1 QEMU smoke PASS** после обновлений. Эти результаты относятся к тому локальному коду и **не являются автоматически новым Gate для GitHub SHA**.
- Пользователь загрузил свежий `main`; в GitHub на дату аудитa появились системный IFS-образ, installer и pinned IVX.
- Три бренд-изображения созданы и помещены пользователем в `/home/quaxis/IntellectOS/media/img`, но в эту документационную операцию их бинарные файлы не копируются.

## 4. Проверки после применения документации

```
git status -sb
make clean check
make uefi-u1-smoke
make uefi-ifs-smoke
make uefi-install-smoke
```

**Команды запускать на Ubuntu локально**, после установки зависимостей, и фиксировать их выход. Переименование документации само по себе не изменяет ядро, firmware, `.ivx` или дисковые образы. Установка на настоящий HDD допускается только после gate 14 и ручной идентификации целевого диска.
