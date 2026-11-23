# Аппаратная совместимость и драйверная система

## 1. Правило поддержки: generic contracts, конкретные adapters

IntellectOS должна работать на максимально широком спектре x86-64 машин, а впоследствии — на других ISA через отдельные порты. Но ни один код не может поддержать физически любой компьютер без драйверов. «Универсальность» означает отсутствие необоснованных hard-coded предположений и **проверяемую матрицу**: CPU/ISA, firmware, ACPI/PCI, storage, display, input, networking, audio, power, recovery.

Нельзя привязывать ядро к AMD A6/Lenovo, Intel PIIX, одному GOP mode, 1920×1080, одному типу диска, одному порту COM1 или единственной USB-модели. При отсутствии драйвера допустимы явный degraded mode, serial fallback или отказ с причиной. Нельзя ложно обещать поддержку AHCI/NVMe, если firmware Block I/O есть лишь до ExitBootServices.

## 2. Discovery != Driver

**H1**: bounded CPU/vendor/features, ACPI RSDP/SDT checksum, PCI cfg1/ECAM discovery (с валидными BAR/range), SMBIOS diagnostics; перечисление не предоставляет права DMA/interrupt. **H2**: реестр драйверов с capability объявлениями, ownership, init/bind, state, version, remove, permissions и health. Начальный набор: polling UART16550, GOP handoff pixels, PCI read-only, ACPI table validator, PIIX ATA read-only. Следующие драйверы: AHCI, NVMe, USB xHCI/HID/mass storage, VirtIO blk/net, PS/2, GPU/framebuffer, sound, ethernet/wifi, timing and ACPI power.

## 3. Блочный стек

```
Device discovery -> Driver match + exclusive ownership -> BlockDevice interface
  -> partition table validation -> mount IntellectFS -> VFS -> file handles
```

Общий `BlockDeviceV1`: capacity in logical blocks, logical/physical sector sizes, alignment, max transfer, writable/read-only media, cache/flush/FUA capabilities, error taxonomy, stable device identity, sector range bound, worker priority, queue submission, power-loss promises. Изначально READ-ONLY и QEMU image. К записи переходить только после тестов на fake device, power-loss simulator, signed installation policy и explicit target selection.

Текущий S0 `kernel/src64/block64.c` ориентирован на **Intel 8086:7010 PIIX IDE primary master, LBA28, 512 B, polling read-only**, не заменяет NVMe/AHCI/USB. Firmware установщик может использовать UEFI Block I/O до `ExitBootServices`, но установленной системе нужны собственные runtime драйверы или обоснованный transitional adapter.

Для нового железа: AHCI SATA с идентификацией портов и командными слотами; NVMe PCIe MMIO admin/IO queues с PRP/SGL, DMA-safety и timeouts; xHCI и USB mass storage; VirtIO как QEMU/VM провайдер. Обязательные негативные тесты на unplug, timeout, stale completion, buffer overflow, powerfail, read-only device, non-512 LBA, 4Kn/512e, big disks/GPT backup and adapter reset.

## 4. Ввод и вывод

- **Ранний ввод:** COM1 polling, PS/2 при наличии, UEFI Simple Text Input до firmware exit.
- **Native USB:** xHCI core + hub/port enumerate + HID boot keyboard/mouse + device hotplug; не подменять «работает USB в меню UEFI» работающей клавиатурой userland.
- **TTY/terminal:** независимые backends serial, framebuffer, USB HID, remote. Система должна запускаться без 3D GPU.
- **Video:** GOP early framebuffer, modes/format enumeration, clipping, width/height/pitch. Позже KMS/compositor/GPU acceleration, multiple monitors, HiDPI, rotations and input mapping. Нет фиксированной «главной» геометрии.
- **Audio:** optional output driver (например Intel HDA/HD Audio и другие по обнаружению), short startup chime only when supported, volume/mute and power-safe fallback; отсутствие звука никогда не является ошибкой boot.
- **Networking:** отдельный `netdev` и стек L2/L3/TCP/UDP, DNS, firewall/permissions, remote TTY with authentication; не смешивать с network requirements kernel startup.

## 5. Топология CPU и memory protection

ACPI MADT/HPET/FADT и таймеры валидировать до использования. Не делать ACPI AML в раннем bootstrap без проверенного interpreter. APIC/IOAPIC/MSI/MSI-X, IRQ masks/ack, race-free hotplug, IOMMU/DMA protections — обязательны для надёжных драйверов. Переход single CPU → SMP отдельный проверяемый gate: per-CPU state, locks, TLB shootdowns, affinity, fair scheduler, interrupts and timer calibration.

## 6. Тестовая матрица

| Профиль | Минимальная проверка | Назначение |
|---|---|---|
| QEMU/OVMF + PIIX | install image + ring3 + IFS, cold/hot reset | быстрая регрессия |
| QEMU q35/AHCI/NVMe | native storage attach/read/boot | современные шины |
| USB xHCI + keyboard/mouse | install + user input after EBS | без PS/2/COM1 |
| UEFI GOP sizes/formats | 4:3, 5:4, 16:10, wide, ultrawide, portrait, small | адаптивный дизайн |
| Lenovo IdeaPad | физический smoke + independent HDD install | конкретный стенд |
| иные vendor/firmware/CPU | независимый boot/install and low-resource mode | реальная переносимость |
| BIOS legacy | independent boot/status and fallback | старая техника |

**Запрещено** объявлять «любой ПК» по одному Lenovo. Поддержка только после списка подтверждённых интерфейсов и успешных аппаратных матриц.
