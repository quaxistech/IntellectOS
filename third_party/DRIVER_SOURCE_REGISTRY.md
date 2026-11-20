# IntellectOS: verified upstream driver-source candidates (L1)

**Status: reference sources, NOT native drivers.** All links originate from official upstream projects. Run `python3 tools/vendor_reference_drivers.py --list` to inspect exact source URLs and `--fetch` to download pinned reference sources and license files into `third_party/reference/` (requires Internet). No source is linked into the kernel by this operation. Preserve upstream copyright and SPDX notices when porting code.

| Area | Authoritative source | License family | Can operate after IntellectOS ExitBootServices? |
|---|---|---|---|
| PCI enumeration/driver binding | [TianoCore EDK II PciBusDxe](https://github.com/tianocore/edk2/tree/master/MdeModulePkg/Bus/Pci/PciBusDxe) | Predominantly BSD-2-Clause-Patent; check every file | **No**, UEFI Boot Services-dependent |
| USB xHCI host controller | [TianoCore XhciDxe](https://github.com/tianocore/edk2/tree/master/MdeModulePkg/Bus/Pci/XhciDxe) | File SPDX, typically BSD-2-Clause-Patent | **No**, needs native DMA, MMIO and interrupts port |
| USB bus & devices | [TianoCore UsbBusDxe](https://github.com/tianocore/edk2/tree/master/MdeModulePkg/Bus/Usb/UsbBusDxe) | File SPDX | **No**, requires UEFI driver model |
| NVMe controller | [TianoCore NvmExpressDxe](https://github.com/tianocore/edk2/tree/master/MdeModulePkg/Bus/Pci/NvmExpressDxe) | File SPDX | **No**, requires UEFI protocols and services |
| Portable USB host core | [TinyUSB](https://github.com/hathach/tinyusb/tree/master/src/host) | MIT | **Not without** controller back-end, IRQ and platform integration |
| Native xHCI reference | [FreeBSD xHCI](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/usb/controller/xhci.c) | BSD-2-Clause per file | **Not directly**, depends on FreeBSD kernel APIs |
| Native NVMe reference | [FreeBSD NVMe](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/nvme/nvme.c) | BSD-2-Clause per file | **Not directly**, depends on FreeBSD kernel APIs |

Provenance is fixed by upstream commits embedded in `tools/vendor_reference_drivers.py`. The source fetch step is **optional and is not a build dependency**. FreeBSD is selected over copying Linux kernel drivers wholesale because Linux kernel sources are generally GPL-2.0-only; using those sources may impose obligations incompatible with the project's intended license. License review is per-file, not merely per-repository.

## Real IntellectOS functionality today

- UEFI bootloader uses the machine's already-installed firmware drivers **until** `ExitBootServices`.
- In the independent kernel, C1 implements a 16550 UART (if present); Stage E uses a handed-off GOP framebuffer for software-rendered pixels; H1 reads CPUID, ACPI and PCI configuration; S0 has a limited read-only ATA PIO driver for legacy PIIX in QEMU.
- The boot screen is **not** evidence of a native GPU driver, USB HID stack, NVMe controller or broad hardware support.
- For Lenovo hardware, actual chipset identification must drive the subsequent source selection/porting. NVMe/AHCI/xHCI devices remain detected but unbound until native controller drivers exist.

## Invariants before native controller attach

1. Bounded MMIO/BAR mapping with UC attributes and resource ownership.
2. DMA-capable memory allocations and IOMMU awareness or strict software limits; zero untrusted buffers.
3. Interrupt/APIC/MSI paths (or bounded controller polling with timeouts) and race-safe teardown.
4. No arbitrary PCI configuration writes during discovery.
5. No physical disk writes before approved installer steps and crash-safe storage.
6. Device-specific tests on QEMU fixtures then on Lenovo, reporting PASS/FAIL/NOT_SUPPORTED per device.
