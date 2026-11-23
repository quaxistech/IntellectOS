# Boot, firmware и BootInfo ABI

## 1. Две точки входа, один продукт

**UEFI x86-64 — первичная целевая платформа для Terminal Edition.** `EFI/BOOT/BOOTX64.EFI` как PE32+ приложение обнаруживает GOP, карту памяти, ACPI/SMBIOS, EFI storage capabilities и клавиатуру, строит валидный BootInfo, резервирует память под kernel/stack/page tables, получает последнюю memory map и вызывает `ExitBootServices`; дальше ядро независимо. BIOS Legacy сохраняем как собственную альтернативу для поддерживаемой старой аппаратуры. Legacy BIOS путь сейчас использует самостоятельный MBR+Stage1 и i386 kernel; он НЕ уже объединён с 64-bit user/kernel. Требование будущего единого x86-64 kernel не должно ломать работающий i386 diagnostic fallback.

## 2. BootInfo v1 — существующий контракт

Нормативное C-описание `kernel/include/bootinfo.h`: little-endian, header **152 bytes**, memory descriptors **24 bytes** отдельно; `magic=0x49534F33`, version=1. Заголовок включает type BIOS/UEFI, flags, boot drive, firmware/UEFI revisions, framebuffer base/size/width/height/**pitch**/pixel_format, `memory_map_addr/count/stride`, ACPI RSDP, SMBIOS, vendor. Все адреса — физические `u64`, а не указатели на живые UEFI services.

- Проверка `magic/version/header_size/count/stride` и overflow до разыменования.
- `framebuffer_pitch` не приравнивать к `width*4`; проверять buffer size, форматы, потенциальный overflow и device mapping.
- Карта памяти нормализуется, перекрытия и firmware/MMIO/reserved не становятся свободными RAM.
- ABI **не** является User ABI, IVX ABI или Vibelang ABI. Они имеют отдельные реестры и тесты.
- Изменение BootInfo возможно лишь после совместной проверки BIOS+UEFI producers, x64 consumer и независимых fixtures; не увеличивать major по косметической причине.

## 3. UEFI безопасный handoff

До выхода доступны `SimpleTextInput`, `BlockIO`, GOP, время и другие протоколы. Загрузчик использует вызовы с правильным EFIAPI (`ms_abi`), таблицы правильных смещений (`LocateProtocol` на +320 для текущего x64 интерфейса) и не хранит их после выхода. Нельзя выделять память или печатать через Boot Services между финальным `GetMemoryMap` и `ExitBootServices` так, чтобы сломать key. При `EFI_INVALID_PARAMETER` повторять bounded retry; после частичной остановки прошивки не вызывать потенциально недоступные сервисы.

Стартовое bootstrap identity mapping — не постоянная модель памяти. Стадии M1/M2/M3-A разделяют executable kernel TEXT, readonly RODATA, RW+NX DATA/heap, guard stack и supervisor-only mappings; проверять CR0.WP, NXE, TLB и физические алиасы. Витрины UI не вмешиваются в эту последовательность.

## 4. Схема диска установленной системы

Стандартная GPT: protective MBR; primary+backup GPT headers и partition arrays с CRC; EFI System Partition (FAT32, UEFI standard); отдельный IntellectFS partition с зафиксированным GUID/type и системным томом; при необходимости recovery/boot slots. ESP хранит загрузчик и файлы, необходимые firmware; основной native root — **IntellectFS**, не переименованный FAT32. Никаких параллельных «истин» файловой конфигурации. ESP не должен считаться секретным хранилищем.

`EFI/BOOT/BOOTX64.EFI` — fallback path, boot entry через UEFI NVRAM предпочтителен после валидной регистрации; отказ NVRAM/variable write должен иметь явный fallback. Secure Boot: сейчас unsigned developer binaries могут не загрузиться с включённым Secure Boot; production нужен собственный подписанный доверенный boot-chain и политически управляемые ключи.

## 5. Boot modes и UI

Live Terminal без записи; Installer с явной операцией записи; Recovery без автоматического изменения накопителя; будущий graphical desktop как отдельная служба, а не отдельный kernel. Установочное меню до ExitBootServices может использовать GOP+USB клавиатуру посредством прошивки. Native ОС после выхода обязана иметь собственный драйвер либо иной подтверждённый способ ввода; предзагрузочный USB input НЕ доказывает runtime xHCI/HID.

## 6. Ошибки и тесты

Условия отказа: неверная структура UEFI, unsupported GOP, изменившийся map key, недостаточно памяти, отсутствие подходящего boot volume, invalid GPT/IFS seal, IVX wrong signature, storage driver absent, host device unexpectedly selected. Система должна остановиться **с диагностикой**, а не падать в произвольный код или выбирать первый диск молча.

Тесты: UEFI host ABI fixtures, независимая проверка EFI PE32+, ROM/OVMF boot, `ExitBootServices` runtime log, физический Lenovo и ещё разные vendor/UEFI chipset tests, запуск с HDD без USB, Secure Boot on/off policy, отсутствие GOP (headless pathway), изменение framebuffer geometry, BIOS fallback отдельно. Логические критерии — `14_TESTING_AND_RELEASE`.
