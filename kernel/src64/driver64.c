/* H2: bootstrap driver-manager state with capability labels.
 * Reuses devices that were initialized by verified C1/UEFI handoff;
 * PCI inspection does not masquerade as NVMe/xHCI/AHCI functionality.
 */
#include <stddef.h>
#include <stdint.h>
#include "driver64.h"

static intellect_driver64_record_t drivers[INTELLECT_DRIVER64_COUNT+1u];
static int registered;
static intellect_driver64_claim_t claims[16];
static unsigned used;
int intellect_driver64_claim(unsigned driver, unsigned domain,
                            uint64_t base, uint64_t length) {
    if (!registered || driver<1u || driver>INTELLECT_DRIVER64_COUNT ||
        drivers[driver].state!=INTELLECT_DRIVER64_ACTIVE ||
        (domain!=INTELLECT_DRIVER64_PIO && domain!=INTELLECT_DRIVER64_MMIO) ||
        !length || base>UINT64_MAX-length || used>=16u) return 0;
    if(domain==INTELLECT_DRIVER64_PIO && (base>65535u || length>65536u-base)) return 0;
    for (unsigned i=0;i<used;++i) {
        const intellect_driver64_claim_t *r=&claims[i];
        if (r->domain==domain && base<r->base+r->length && r->base<base+length)
            return 0;
    }
    claims[used++]=(intellect_driver64_claim_t){
        (uint8_t)driver,(uint8_t)domain,0u,base,length
    };
    return 1;
}
unsigned intellect_driver64_claim_count(void) {return used;}
int intellect_driver64_begin_pio_ata(void) {
    if(!registered || used>14u ||
       drivers[INTELLECT_DRIVER64_ATA_PIO].state!=INTELLECT_DRIVER64_UNAVAILABLE) return 0;
    const uint64_t bases[2]={0x1F0u,0x3F6u};
    const uint64_t lengths[2]={8u,1u};
    for(unsigned r=0;r<2u;++r) for(unsigned i=0;i<used;++i) {
        const intellect_driver64_claim_t *c=&claims[i];
        if(c->domain==INTELLECT_DRIVER64_PIO &&
           bases[r]<c->base+c->length && c->base<bases[r]+lengths[r]) return 0;
    }
    for(unsigned r=0;r<2u;++r) claims[used++]=(intellect_driver64_claim_t){
        INTELLECT_DRIVER64_ATA_PIO,INTELLECT_DRIVER64_PIO,0u,bases[r],lengths[r]};
    drivers[INTELLECT_DRIVER64_ATA_PIO].state=INTELLECT_DRIVER64_PROBING;
    return 1;
}
int intellect_driver64_finish_pio_ata(int success) {
    intellect_driver64_record_t *d=&drivers[INTELLECT_DRIVER64_ATA_PIO];
    if(d->state!=INTELLECT_DRIVER64_PROBING || used<2u ||
       claims[used-2u].owner!=INTELLECT_DRIVER64_ATA_PIO ||
       claims[used-1u].owner!=INTELLECT_DRIVER64_ATA_PIO) return 0;
    if(!success) {
        used-=2u; d->state=INTELLECT_DRIVER64_UNAVAILABLE;
        return 1;
    }
    d->state=INTELLECT_DRIVER64_ACTIVE; d->attached=1u;
    d->capabilities=8u; /* readonly 512B block */
    return 1;
}
int intellect_driver64_init(const intellect_hw64_snapshot_t *hw) {
    if (!hw || registered) return 0;
    for (unsigned i=1;i<=INTELLECT_DRIVER64_COUNT;++i) drivers[i].id=(uint8_t)i;
    /* No callback installation at fixed absolute addresses in flat PIE:
     * registration is by stable numeric driver ID. */
    drivers[INTELLECT_DRIVER64_UART].state=hw->serial_ready;
    drivers[INTELLECT_DRIVER64_UART].capabilities=3u;
    drivers[INTELLECT_DRIVER64_UART].attached=hw->serial_ready?1u:0u;
    drivers[INTELLECT_DRIVER64_FRAMEBUFFER].state=hw->framebuffer_ready;
    drivers[INTELLECT_DRIVER64_FRAMEBUFFER].capabilities=5u;
    drivers[INTELLECT_DRIVER64_FRAMEBUFFER].attached=hw->framebuffer_ready?1u:0u;
    drivers[INTELLECT_DRIVER64_PCI_CONFIG].state=hw->pci_cfg1_ready;
    drivers[INTELLECT_DRIVER64_PCI_CONFIG].capabilities=1u;
    drivers[INTELLECT_DRIVER64_PCI_CONFIG].attached=hw->pci_cfg1_ready?1u:0u;
    drivers[INTELLECT_DRIVER64_ACPI_TABLES].state=hw->acpi_root_valid;
    drivers[INTELLECT_DRIVER64_ACPI_TABLES].capabilities=1u;
    drivers[INTELLECT_DRIVER64_ACPI_TABLES].attached=hw->acpi_root_valid?1u:0u;
    drivers[INTELLECT_DRIVER64_ATA_PIO].state=INTELLECT_DRIVER64_UNAVAILABLE;
    registered=1;
    if (hw->serial_ready &&
        !intellect_driver64_claim(INTELLECT_DRIVER64_UART,
                         INTELLECT_DRIVER64_PIO,0x3F8u,8u)) return 0;
    if (hw->pci_cfg1_ready &&
        !intellect_driver64_claim(INTELLECT_DRIVER64_PCI_CONFIG,
                         INTELLECT_DRIVER64_PIO,0xCF8u,8u)) return 0;
    if (hw->framebuffer_ready && hw->framebuffer_addr &&
        hw->framebuffer_size &&
        !intellect_driver64_claim(INTELLECT_DRIVER64_FRAMEBUFFER,
                         INTELLECT_DRIVER64_MMIO,
                         hw->framebuffer_addr,hw->framebuffer_size)) return 0;
    return 1;
}
const intellect_driver64_record_t *intellect_driver64_get(unsigned id) {
    if (!registered || !id || id>INTELLECT_DRIVER64_COUNT) return NULL;
    return &drivers[id];
}
const char *intellect_driver64_name(unsigned id) {
    switch(id) {
        case INTELLECT_DRIVER64_UART: return "uart16550-polling";
        case INTELLECT_DRIVER64_FRAMEBUFFER: return "gop-framebuffer-handoff";
        case INTELLECT_DRIVER64_PCI_CONFIG: return "pci-config-type1-readonly";
        case INTELLECT_DRIVER64_ACPI_TABLES: return "acpi-table-validator";
        case INTELLECT_DRIVER64_ATA_PIO: return "piix-ata-pio-readonly";
        default: return "unknown";
    }
}
const char *intellect_driver64_pci_class(const intellect_hw64_pci_t *p) {
    if (!p) return "invalid";
    if (p->class_code==1u) {
        if (p->subclass==8u && p->prog_if==2u) return "NVMe";
        if (p->subclass==6u && p->prog_if==1u) return "AHCI";
        if (p->subclass==1u) return "IDE";
        return "storage";
    }
    if (p->class_code==2u) return "network";
    if (p->class_code==3u) return "display";
    if (p->class_code==6u) return p->subclass==4u ? "PCI-bridge" : "host-bridge";
    if (p->class_code==0x0Cu && p->subclass==3u) {
        if (p->prog_if==0x30u) return "xHCI";
        if (p->prog_if==0x20u) return "EHCI";
        if (p->prog_if==0x10u) return "OHCI";
        if (p->prog_if==0x00u) return "UHCI";
        return "USB-controller";
    }
    if (p->class_code==4u) return "multimedia";
    return "other";
}
const char *intellect_driver64_pci_requirement(const intellect_hw64_pci_t *p) {
    if (!p) return "invalid";
    if (p->class_code==1u && p->subclass==8u && p->prog_if==2u)
        return "nvme-driver-needed";
    if (p->class_code==1u && p->subclass==6u && p->prog_if==1u)
        return "ahci-driver-needed";
    if (p->class_code==0x0Cu && p->subclass==3u) {
        if (p->prog_if==0x30u) return "xhci-driver-needed";
        if (p->prog_if==0x20u) return "ehci-driver-needed";
        if (p->prog_if==0x10u) return "ohci-driver-needed";
        if (p->prog_if==0x00u) return "uhci-driver-needed";
        return "usb-controller-driver-needed";
    }
    if (p->class_code==2u) return "network-driver-needed";
    return "no-controller-driver";
}
