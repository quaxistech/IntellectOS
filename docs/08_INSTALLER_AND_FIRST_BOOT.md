# Установщик, конфигурация, первый запуск и восстановление

## 1. Целевая последовательность

```
USB image boots independently → UEFI/BIOS menu → Live Terminal / Install
→ detect storage and supported input/display → select explicit target
→ partition view + planned operations + irreversible-change preview
→ user confirms exact target identity → GPT + FAT32 ESP + IntellectFS volume
→ copy and verify bootloader, root, /sbin/init.ivx, terminal.ivx, trust and config
→ validate GPT copies/IFS seal/IVX signatures → register UEFI entry or fallback
→ clean reboot with USB REMOVED → IFS mount → init → optional root login
→ verified terminal.ivx in ring3; recoverable errors are explicit
```

Installer must not depend on the developer's Ubuntu after startup. Target firmware UEFI may provide pre-exit Block I/O/GOP/input even before native drivers. That does **not** prove native storage after handoff. On unsupported hardware, show a reason and abort without partial destructive writing wherever feasible.

## 2. Пользовательский процесс

- Экран 1: выбрать **Live Terminal** (без записи), **Install** (диск будет изменён), **Recovery** (безопасный режим), будущее Graphical Edition отдельно от возможности показывать графический установщик.
- Экран 2: обнаруженные диски/разделы (model, capacity, bus, stable device identifier, sector size, removable/read-only, mount status, SMART if supported). Не угадывать системный диск по `/dev/sda`: имена нестабильны. Отдельно защищать USB с установщиком, текущий boot disk и важные установленные ОС.
- Экран 3: варианты **полный диск** и **ручное управление разделами** (создать/удалить/изменить, выбор ESP/IFS/recovery, превью изменений) — ручной режим включить только при реальных проверенных writer/partition ops. Сейчас host `target_disk_installer.py` преимущественно full-disk image/verified limited target, не готовый редактор произвольной GPT.
- Экран 4: часовой пояс IANA и RTC UTC, пользователь `root` и пароль **любой длины от 0 до 256 UTF-8 bytes**, без требований цифр/регистра/12 символов. Если выбран пустой — отдельное подтверждение и предупреждение о риске; не блокировать локальную разработку.
- Экран 5: план/подтверждение формата/целевой модель-диск, staged writes, прогресс по реальным этапам, проверка возвратного чтения, сохранение diagnostics.
- Экран 6: сообщить, что запись и верификация завершены, предложить извлечь USB, выполнить независимый boot test.

## 3. Политика `root` и пароля

`root` uid=0. Пустой пароль является допустимой сознательно выбранной конфигурацией. **Не хранить строку открытым текстом**, ключи или shell history. Непустой пароль (и при желании пустой в единообразной схеме) → Argon2id record with unique salt and bounded cost; reader должен отличать «пустой пароль разрешён» от «corrupt auth database, разрешить всем» и никогда не делать silent fallback. `login` kernel/session service должен проверять credential и выдавать principal/capabilities только после политики; host `provision()` это не runtime login.

При пустом пароле пользователь может нажать Enter; для single-user dev возможен auto-login, но это отдельный persisted toggle и требует согласованного capability policy. Для серверной/удалённой конфигурации пустой пароль **никогда** не должен случайно разрешать удалённый вход; remote auth политика независима от локальной password policy. Password upgrade/rotate/recovery — отдельные транзакции IFS.

## 4. Идентичность времени

IANA timezone e.g. `Europe/Kyiv`, `Asia/Tokyo`, `Etc/UTC`, сохранять canonical string в `/etc/system.json` и `/etc/timezone`, RTC policy UTC. Host zoneinfo проверяет syntax в installer; runtime должен поддерживать timezone database или явно показывать UTC при отсутствии, не имитировать локальное время. Monotonic clock отдельно от civil time.

## 5. Диск и безопасная запись

- Read-only preview and primary/backup GPT CRC, protective MBR; согласовать IFS GUID/code.
- Reserved aligned sectors, EFI FAT32 as firmware-expected, IFS root 4KiB aligned; sector size/geometry support explicit.
- Verify complete bytes `/EFI/BOOT/BOOTX64.EFI`, strict IFS mount/commit/seal, trusted public key/signature and `init/terminal` dependencies.
- Real write gated by device identity, explicit typed confirmation and no ambiguous «first drive». Device writes fail-closed on timeout/short write/no flush, with ability to resume or restore known-good state; do not claim atomicity beyond controller guarantees.
- Development disk image (ordinary file) and physical `/dev/*` paths have **different safety boundaries**. Automated tests must not write host devices by default.

## 6. Recovery and first-boot errors

No boot media, invalid ESP, corrupt IFS seal, missing `init`/`terminal`, unexpected EFI variable state, wrong IVX pin/signature, disabled Secure Boot chain, no native disk after EBS — different error messages. Recovery should preserve previous sealed root/boot slot and offer diagnostic userland/reinstall with explicit consent. Do not accept arbitrary unsigned fallback image to hide the problem. If no display, headless serial diagnostics when hardware supports it.

## 7. Gates

**HDD bootstrap gate:** image generated and verified; QEMU boots from HDD **without USB** and enters U1. **Installed system gate:** actual IFS file terminal verified and run; persisted root/timezone read; login works as configured; QEMU reset and hardware run; negative tests. **Universal support gate:** several real PC classes including native NVMe/AHCI and USB input after EBS. The first does not imply the next two.
