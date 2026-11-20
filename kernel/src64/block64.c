/* H2/S0 storage: observationally bind only known QEMU-compatible PIIX IDE.
 * No generic controller writes, no DMA, no firmware calls after handoff.
 */
#include <stddef.h>
#include <stdint.h>
#include "block64.h"
#include "ata_pio64.h"
#include "driver64.h"

static intellect_block64_info_t disk;
static intellect_ata64_ops_t backend;
static int attempted;
#ifndef INTELLECT_BLOCK64_HOST_TEST
static __attribute__((used)) uint8_t port_in8(void *ctx,uint16_t port) {
    (void)ctx;uint8_t result;
    __asm__ volatile("inb %w1,%0":"=a"(result):"Nd"(port):"memory");
    return result;
}
static __attribute__((used)) uint16_t port_in16(void *ctx,uint16_t port) {
    (void)ctx;uint16_t result;
    __asm__ volatile("inw %w1,%0":"=a"(result):"Nd"(port):"memory");
    return result;
}
static __attribute__((used)) void port_out8(void *ctx,uint16_t port,uint8_t value) {
    (void)ctx;
    __asm__ volatile("outb %0,%w1"::"a"(value),"Nd"(port):"memory");
}
#endif
static int bind(const intellect_ata64_ops_t *ops) {
    intellect_ata64_info_t info;
    if(!intellect_ata64_identify(ops,&info)) return 0;
    backend=*ops;
    disk.sectors=info.sectors;
    disk.sector_bytes=INTELLECT_BLOCK64_SECTOR_BYTES;
    disk.readonly=1u;
    disk.ready=1u;
    for(unsigned i=0;i<sizeof(disk.model);++i) disk.model[i]=info.model[i];
    return 1;
}
int intellect_block64_piix_supported(const intellect_hw64_snapshot_t *hw) {
    if(!hw || !hw->pci_cfg1_ready || hw->pci_truncated ||
       hw->pci_count>INTELLECT_HW64_MAX_PCI) return 0;
    for(unsigned i=0;i<hw->pci_count;++i) {
        const intellect_hw64_pci_t *p=&hw->pci[i];
        if(p->vendor_id==0x8086u && p->device_id==0x7010u &&
           p->class_code==1u && p->subclass==1u &&
           (p->header_type&0x7Fu)==0u && !(p->prog_if&1u)) return 1;
    }
    return 0;
}
int intellect_block64_init(const intellect_hw64_snapshot_t *hw) {
    if(attempted || !intellect_block64_piix_supported(hw)) return 0;
    attempted=1;
#ifndef INTELLECT_BLOCK64_HOST_TEST
    /* The loader copies a flat PIE WITHOUT applying absolute relocations.
     * Build every function pointer at runtime using RIP-relative LEA.
     * A C aggregate initializer was lowered by clang to .data.rel.ro
     * with R_X86_64_64 and would silently corrupt the jump targets. */
    uintptr_t in8_addr,in16_addr,out8_addr;
    __asm__ volatile("lea port_in8(%%rip),%0" : "=r"(in8_addr));
    __asm__ volatile("lea port_in16(%%rip),%0" : "=r"(in16_addr));
    __asm__ volatile("lea port_out8(%%rip),%0" : "=r"(out8_addr));
    intellect_ata64_ops_t io;
    io.context=NULL;
    io.in8=(uint8_t (*)(void *,uint16_t))in8_addr;
    io.in16=(uint16_t (*)(void *,uint16_t))in16_addr;
    io.out8=(void (*)(void *,uint16_t,uint8_t))out8_addr;
    if(!intellect_driver64_begin_pio_ata()) return 0;
    int ok=bind(&io);
    if(!intellect_driver64_finish_pio_ata(ok)) {
        disk.ready=0u;
        return 0;
    }
    return ok;
#else
    return 0; /* Host must use an explicitly supplied fake ATA controller. */
#endif
}
#ifdef INTELLECT_BLOCK64_HOST_TEST
int intellect_block64_host_init(const intellect_ata64_ops_t *ops) {
    if(attempted) return 0;
    attempted=1;
    return bind(ops);
}
#endif
const intellect_block64_info_t *intellect_block64_info(void) {
    return disk.ready?&disk:NULL;
}
int intellect_block64_read(uint64_t lba,uint8_t out[512]) {
    if(!disk.ready || !out || lba>=disk.sectors || lba>=0x10000000u) return 0;
    return intellect_ata64_read512(&backend,(uint32_t)lba,
                                    (uint32_t)disk.sectors,out);
}
