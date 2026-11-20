# IntellectOS — H2.1 / Storage S0: первый независимый блочный драйвер

**Дата:** 2026-11-20  
**База:** M1 + M2 + M3-A + C1 + H1/H2  
**Статус:** кандидат, требует интегрированного `make clean check` и QEMU smoke на Ubuntu.  
**BootInfo v1:** не меняется. **BIOS 0.4:** не меняется. **IntellectFS:** не реализуется в этом этапе.

## 1. Зачем это необходимо

H1/H2 доказал, что ядро независимо от EFI Boot Services видит CPU/ACPI/PCI,
но раньше не могло использовать контроллер диска. В этом этапе IntellectOS
впервые получает реальный read-only блочный интерфейс после `ExitBootServices()`.

Цель — в безопасном QEMU прочитать LBA0 (MBR), сектор загрузки первого
подходящего раздела (PBR) и проверить отсутствие или валидность первичного GPT.
Тест выполняется **только над файлом** `build/intellectos-uefi-stage-e.img`.
Скрипт не использует `dd`, не пишет на хостовые `/dev/*`, не форматирует диск,
не меняет partition tables. ATA драйвер не содержит команд записи на диск.

## 2. Транспорт и поддержка

- Контроллер: PCI class `01/01` с vendor/device `8086:7010` (Intel PIIX IDE),
  только первичный канал в compatibility mode (`prog_if.bit0 == 0`).
- Устройство: primary master (`0x1F0..0x1F7`, alt status `0x3F6`).
- Команды: `IDENTIFY DEVICE 0xEC`, `READ SECTORS 0x20` (PIO), один сектор за вызов.
- Адресация: LBA28, не более 2^28 логических секторов, 512 байт/сектор.
- No DMA, IRQ, bus mastering, outb to ATA data register, flush, write,
  ATAPI, NCQ, hotplug, UEFI disk protocol or USB storage.
- Работает только в текущем раннем режиме: один CPU, прерывания запрещены,
  нет scheduler. Ограниченные polling loops возвращают ошибку, а не ждут вечно.
- Неизвестный IDE-контроллер и AHCI/NVMe **не опрашиваются** через ошибочно
  предполагаемые legacy I/O порты. Они остаются `UNAVAILABLE`.

## 3. Драйверный контракт

`intellect_block64_init(snapshot)` допускает PIIX IDE только из H1 PCI inventory,
**до доступа к устройству** атомарно резервирует два PIO-диапазона в H2,
переходит в `PROBING`, выполняет `IDENTIFY` и либо подтверждает `ACTIVE`,
либо освобождает оба диапазона и возвращает `UNAVAILABLE`.
При успехе регистрируется новый встроенный драйвер
`piix-ata-pio-readonly` с `ATTACHED=1` и только capability `block-read`.
Несовместимый/неисправный диск — не причина остановить ядро.

`intellect_block64_info()` возвращает ёмкость, модель, размер логического
сектора и явный `readonly` флаг. `intellect_block64_read(lba, output[512])`
возвращает `0` на любой ошибке или недопустимом LBA, не считает частичное
чтение успешным. API только ядровой, не публичный ABI, не IVX syscall.

PIO-команды записывают **только управляющие регистры ATA** (`0x1F2..0x1F7`)
для выбора диска и запуска чтения. Ни один путь не может передавать сектор
из RAM на диск. `READONLY=1` — контракт драйвера, а не гарантия от
внешних ошибок SSD прошивки или злонамеренного аппаратного обеспечения.

## 4. Таблицы разделов

MBR: сигнатура `55 AA`, четыре записи с типом и диапазоном в пределах
ёмкости. Активный флаг только `0x00/0x80`. MBR без подписи или выходящий за
пределы диска — ошибка, не подмена нулевыми разделами. Расширенные EBR
не обходятся автоматически; контейнеры не монтируются.

GPT: только при protective MBR тип `0xEE`; проверяется signature `EFI PART`,
revision 1.0, длина header 92..512, CRC32 заголовка и массива записей,
диапазоны LBA и отсутствие пересечения используемых разделов. Поддерживается
максимум 128 записей размером ровно 128 байт. CRC32 вычисляется потоково, без
выделения памяти. **Резервный GPT header, GPT на дисках с 4-KiБ логическим
сектором, hybrid MBR и восстановление GPT не поддержаны.** GPT-проверка не
разрешает запись, форматирование или автоматическое исправление.

## 5. Диагностика в COM1

```text
disk help
disk info
disk mbr
disk boot
disk gpt
hw drivers
```

`disk boot` читает PBR первого основного не-GPT, не-extended раздела, печатает
LBA, сигнатуру `55AA` и первые восемь байт. Это **не** FAT-драйвер и не
монтирование раздела.

Пример ожидаемого результата в QEMU/OVMF с нашим стандартным IDE-образом:

```text
S0 BLOCK READONLY: READY
intellect# disk mbr
S0 MBR: SIGNATURE=55AA PARTITIONS=1 GPT_PROTECTIVE=0
intellect# disk gpt
S0 GPT: NOT_PRESENT (MBR disk)
```

## 6. Тесты и критерии приёмки

1. `python3 tools/apply_s0.py --project "$PWD" --check` — без изменений файлов.
2. `--apply` — обновляет только 8 предсуществующих исходников/тестов и добавляет
   новые модули. Не делает git операций. Не трогает пользовательские MD.
3. `make clean check` — все старые и новые host-тесты + плоская проверка ELF.
4. `make uefi-hw-smoke` — regression H1/H2.
5. `make uefi-s0-smoke` — QEMU/OVMF получает `S0 BLOCK READONLY: READY`,
   выполняет `disk info`, `disk mbr`, `disk boot`, `disk gpt`, `hw drivers`.
6. Host-тесты при `-O0`/`-O2`: ATA IDENTIFY/READ, LBA bound, MBR,
   первичный GPT, CRC mismatch, отсутствие физического port I/O в тесте.
7. **Никакой тест на Lenovo SSD не проводится на этом этапе.** Контроллеры
   AHCI/NVMe остаются только обнаруженными; запуск на них — после безопасного
   MMIO/IRQ/DMA transport и аппаратной валидации.

## 7. Что дальше

Следующий этап: PCIe MCFG/ECAM с bounded MMIO mappings, PCI capabilities,
ресурсы/IRQ, затем NVMe и/или AHCI чтение с собственным DMA и IOMMU/physical
memory ownership. Для современных серверов и Live USB понадобятся xHCI,
USB Mass Storage, безопасный разделитель процессов и полноценный VFS.
Последующий этап FAT16 **read-only** монтирования EFI-раздела проверит путь
`block -> partition -> filesystem -> VFS`, прежде чем появятся записи.
