#include <stdint.h>
#include "ports.h"
#include "console.h"
#include "pci.h"

/* Legacy PCI configuration mechanism #1 (widely supported on BIOS x86 PCs).
 * Read-only enumeration: writes only to port CF8 to select a config address.
 */
static uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16)
                     | ((uint32_t)device << 11) | ((uint32_t)function << 8)
                     | ((uint32_t)offset & 0xfcu);
    outl(0xCF8u, address);
    return inl(0xCFCu);
}

static const char *pci_class_name(uint8_t cls) {
    switch (cls) {
        case 0x01: return "storage";
        case 0x02: return "network";
        case 0x03: return "display";
        case 0x04: return "multimedia/audio";
        case 0x06: return "bridge";
        case 0x0C: return "serial/USB";
        default: return "other";
    }
}

static void report_function(uint8_t bus, uint8_t device, uint8_t function,
                            uint16_t vendor, uint16_t ident, uint32_t class_reg) {
    uint8_t cls = (uint8_t)(class_reg >> 24);
    console_write("  PCI ");
    console_dec32(bus);
    console_putc(':');
    console_dec32(device);
    console_putc('.');
    console_dec32(function);
    console_write("  vendor=");
    console_hex32((uint32_t)vendor);
    console_write(" device=");
    console_hex32((uint32_t)ident);
    console_write(" ");
    console_write(pci_class_name(cls));
    console_newline();
}

void pci_report(void) {
    uint32_t found = 0u;
    uint32_t printed = 0u;
    console_write("\nPCI devices (detected, not driven):\n");
    for (uint32_t bus = 0; bus < 256u; ++bus) {
        for (uint32_t dev = 0; dev < 32u; ++dev) {
            uint32_t id0 = pci_read((uint8_t)bus, (uint8_t)dev, 0u, 0x00u);
            uint16_t ven0 = (uint16_t)(id0 & 0xffffu);
            if (ven0 == 0xffffu || ven0 == 0x0000u) continue;
            uint32_t hdr = pci_read((uint8_t)bus, (uint8_t)dev, 0u, 0x0Cu);
            uint32_t function_count = (((hdr >> 16) & 0x80u) != 0u) ? 8u : 1u;
            for (uint32_t func = 0; func < function_count; ++func) {
                uint32_t id = (func == 0u) ? id0 :
                    pci_read((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0x00u);
                uint16_t vendor = (uint16_t)(id & 0xffffu);
                if (vendor == 0xffffu || vendor == 0u) continue;
                uint16_t device_id = (uint16_t)(id >> 16);
                ++found;
                if (printed < 3u) {
                    uint32_t class_reg = pci_read((uint8_t)bus, (uint8_t)dev,
                                                   (uint8_t)func, 0x08u);
                    report_function((uint8_t)bus, (uint8_t)dev, (uint8_t)func,
                                    vendor, device_id, class_reg);
                    ++printed;
                }
            }
        }
    }
    console_write("PCI functions found: ");
    console_dec32(found);
    console_newline();
    (void)printed; /* console shows just a short snapshot, full driver support pending */
}
