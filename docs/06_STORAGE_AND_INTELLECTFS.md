# Хранение, VFS и IntellectFS-1

**Полная нормативная цель дискового формата:** `specs/IntellectFS_v1_FULL_ARCHITECTURE_AND_DISK_FORMAT.md` (исходник перенесён побайтово и не сокращён). Этот документ задаёт владельцев и этапы реализации, а не заменяет таблицы полей и crash model.

## 1. IntellectFS — собственная ФС

Объектно-экстентная **CoW** архитектура с OID, каталогами/index, extent mappings, snapshots/reflinks, checksummed metadata/data, verified bootstrap/root pointers, atomic seal publication, transaction protocol `begin → mutate → seal`, rollback на предыдущий root, bounded recovery и `ifsck`. Это не ext4/ZFS/FUSE под другим названием. Цель: целостность, предсказуемая tail latency, малые лишние записи, независимость от Linux и от конкретного SSD.

## 2. Дисковые инварианты IFS-1

- Основная единица on-disk metadata: **4096 bytes**. Не предполагать, что физическая 4KiB запись атомарна; firmware, контроллер и flash translation layer могут разорвать запись.
- Четыре bootstrap copies и 4096-byte seal records; BLAKE3 integrity; little-endian fixed-size records и проверка переполнений; при выборе root требовать согласованную цепь bootstrap/layout/descriptor/seal.
- Публикация: новый CoW tree на свободных блоках; canonical descriptor + retained old root; durability barrier (`Flush/FUA` при честной семантике), верификация readback по требованию; seal создаёт логическую видимость и не подменяет физическое обещание устройства.
- **Allocator единственный:** согласован с commit descriptor/root-set; нельзя делать две независимые таблицы «занято» с несовместимыми поколениями.
- Восстановление: выбрать последний валидный sealed chain с ограниченными scan/bounds, недописанные будущие блоки игнорировать, исходный root остаётся доступным до safe GC frontier. `ifsck --inspect` не пишет; repair требует отдельного подтверждения и протокола.
- Путь `/` — namespace view; идентичность объекта OID; файлы могут перемещаться без смены OID, handles/scoped rights ограничивают доступ.

## 3. Начальный BOOT PROFILE и полный RW

Репозиторий содержит `installer/ifs_disk_v1.py` и C `kernel/src64/ifs_ro64.c` с **ограниченным начальным boot profile**, где formatter создаёт начальную sealed root; C-reader проверяет и читает системные файлы на поддерживаемом block backend. Это не доказательство multi-transaction CoW RW, crash-consistent GC, snapshots/reflinks, encryption, multi-shard, high-concurrency или recovery на произвольном SSD. Полный набор флагов/record layouts ещё требует freeze fixtures, independent readers и adversarial crash testing.

Системные пути первого образа: `/sbin/terminal.ivx`, `/etc/system.json`, `/etc/timezone`, `/etc/trust/developer.pub`. В будущем `/sbin/init.ivx` запускает service supervisor, затем login/session/terminal. Загрузка `.ivx` должна идти из **реального IFS root**, не из EFI-ресурса, скрытого ядра или имитации.

## 4. Native VFS

Монтирование `BlockDevice → GPT partition → IFS strict verified volume → VFS mount table`; отдельный read-only FAT32 backend для ESP и recovery, без смешения системного root. Поддержать canonical paths, directory traversal, file handles, flags, namespaces, open-by-OID, ACL/capabilities, file sizes and UTF-8 normalized names; операции `open`, `read`, `write`, `rename`, `unlink`, `mkdir`, `fsync`, snapshot/transaction определять через один error model и bounded rights. VFS кэш не является самостоятельной authoritative source (content/root generation controls invalidation).

## 5. Данные и скорость

Typed IO requests, batching, read-ahead, page cache, async queues, allocation groups, sequential streaming, DB-like append, metadata locality, compact nodes, checksummed zero copy where safe. Advanced profiles compression/encryption/parallel shards, CXL hints, NUMA, FDP и PGO — **опции после доказанной целостности**; не создавать физическую зависимость от одного процессора/SSD. Бенчмарки: seq/random throughput, p99 latency, fsync, boot cold reads, database, small-file tree, WAF, GC pause, verified CPU budget.

## 6. Стандарт для системного раздела

GPT type GUID документации и кода должен быть единым (current implementation derives UUID5 namespace; до external release freeze byte representation и constant). ESP FAT32 для firmware, IFS root for OS. План раздела допускает дополнительный recovery/swap/data, но производит только те операции, для которых есть formatter+validator. Неподдерживаемая 4Kn geometry или Flush → отказ от destructive install с диагностикой, а не silent truncation.

## 7. Приёмка полной записи

- `mkifs` на файле и настоящем GPT volume, `ifsck --inspect`, независимый reader same bytes, negative fixtures.
- Создание, изменение, переименование, удаление, append, large file, sparse, orphan, ownership and ACL; reboot persistence.
- Перебор power-cut после каждого write/flush/superblock/seal: либо новая целая транзакция, либо старая целая; нет hybrid root. Отдельно тестировать timeout/unknown commit status и exactly-once retry semantics.
- Переполнение extents/index/paths, duplicate OID, cycles, corruption/malicious GPT, full disk/space reservation, GC fence; RAM bounded.
- No claim of powerfail durability if device does not honor Flush/FUA.
- Recovery disk boot and rollback from corruption; host verifier must not depend on kernel reader.
