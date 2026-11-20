/* H1: IntellectOS bounded and read-only platform discovery.
 * Minimal BIOS/UEFI-independent x86-64 PCI configuration mechanism #1.
 * Does NOT touch device BARs, initialize controllers, enable bus mastering,
 * run AML, invoke firmware, write a block device or perform DMA.
 * Strictly one CPU with interrupts disabled until a PCI config lock exists.
 */
#include <stddef.h>
#include <stdint.h>
#include "hardware64.h"
#define PHYS_LIMIT UINT64_C(0x100000000)
#define SDT_MAX 65536u
#define RSDP_MAX 4096u

static intellect_hw64_snapshot_t inventory;
static int inventory_ready;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
           (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | (uint64_t)le32(p+4)<<32;
}
static int equal(const uint8_t *p, const char *s, size_t n) {
    for (size_t i=0;i<n;++i) if (p[i]!=(uint8_t)s[i]) return 0;
    return 1;
}
static int checksum(const uint8_t *p, size_t n) {
    uint8_t sum=0;
    for (size_t i=0;i<n;++i) sum=(uint8_t)(sum+p[i]);
    return sum==0;
}
int intellect_hw64_rsdp_parse(const uint8_t *b, size_t n,
                             uint64_t *root, int *xsdt) {
    if (!b || !root || !xsdt || n<20u || !equal(b,"RSD PTR ",8u) ||
        !checksum(b,20u)) return 0;
    uint32_t rsdt=le32(b+16u);
    *root=0u;*xsdt=0;
    if (b[15]>=2u) {
        if (n<36u) return 0;
        uint32_t length=le32(b+20u);
        if (length<36u || length>RSDP_MAX || length>n ||
            !checksum(b,length)) return 0;
        uint64_t extended=le64(b+24u);
        if (extended && extended<PHYS_LIMIT) {
            *root=extended; *xsdt=1; return 1;
        }
    }
    if (!rsdt) return 0;
    *root=rsdt;
    return 1;
}
int intellect_hw64_sdt_validate(const uint8_t *b, size_t n, uint32_t *size) {
    if (!b || !size || n<36u) return 0;
    const uint32_t length=le32(b+4u);
    if (length<36u || length>SDT_MAX || (size_t)length>n ||
        !checksum(b,length)) return 0;
    *size=length;return 1;
}

size_t intellect_hw64_scan_pci(intellect_hw64_pci_read_t read, void *ctx,
                             intellect_hw64_pci_t *out, size_t cap,
                             int *truncated) {
    if (truncated) *truncated=0;
    if (!read || (!out && cap)) return 0;
    size_t count=0;
    /* Scan every legacy segment-0 bus: works without AML root-bridge decode.
     * At most 8192 function-0 probes plus additional multifunction probes.
     * This does not discover segment >0 or ECAM-only devices.
     */
    for (unsigned bus=0;bus<256u;++bus) {
        for (unsigned dev=0;dev<32u;++dev) {
            const uint32_t first=read(ctx,(uint8_t)bus,(uint8_t)dev,0u,0u);
            if ((first&0xFFFFu)==0xFFFFu || (first&0xFFFFu)==0u) continue;
            const uint32_t head=read(ctx,(uint8_t)bus,(uint8_t)dev,0u,12u);
            const unsigned funcs=(head & UINT32_C(0x00800000)) ? 8u : 1u;
            for (unsigned fn=0;fn<funcs;++fn) {
                uint32_t id=fn ? read(ctx,(uint8_t)bus,(uint8_t)dev,(uint8_t)fn,0u) : first;
                if ((id&0xFFFFu)==0xFFFFu || (id&0xFFFFu)==0u) continue;
                uint32_t cls=read(ctx,(uint8_t)bus,(uint8_t)dev,(uint8_t)fn,8u);
                uint32_t hdr=fn ? read(ctx,(uint8_t)bus,(uint8_t)dev,(uint8_t)fn,12u) : head;
                if (count>=cap) { if (truncated) *truncated=1; return count; }
                intellect_hw64_pci_t *p=&out[count++];
                p->bus=(uint8_t)bus;p->device=(uint8_t)dev;p->function=(uint8_t)fn;
                p->vendor_id=(uint16_t)id;p->device_id=(uint16_t)(id>>16);
                p->revision=(uint8_t)cls;p->prog_if=(uint8_t)(cls>>8);
                p->subclass=(uint8_t)(cls>>16);p->class_code=(uint8_t)(cls>>24);
                p->header_type=(uint8_t)(hdr>>16);
                p->secondary_bus=0;
                if (p->class_code==6u && p->subclass==4u) {
                    uint32_t buses=read(ctx,(uint8_t)bus,(uint8_t)dev,(uint8_t)fn,24u);
                    p->secondary_bus=(uint8_t)(buses>>8);
                }
            }
        }
    }
    return count;
}


int intellect_hw64_range_allowed(const intellect_memory_range_t *r,
                  size_t count, uint64_t address, size_t length,
                  uint64_t rsdp_address) {
    if (!r || !count || count>5300u || !length ||
        address<0x1000u || address>=PHYS_LIMIT ||
        (uint64_t)length>PHYS_LIMIT-address) return 0;
    for(size_t i=0;i<count;++i) {
        if (!r[i].length || r[i].base>UINT64_MAX-r[i].length) continue;
        if(r[i].type!=INTELLECT_MEMORY_ACPI_RECLAIM &&
           r[i].type!=INTELLECT_MEMORY_ACPI_NVS &&
           !(r[i].type==INTELLECT_MEMORY_RESERVED &&
             address==rsdp_address && length<=RSDP_MAX)) continue;
        if(address>=r[i].base && address-r[i].base<=r[i].length &&
           (uint64_t)length<=r[i].length-(address-r[i].base)) return 1;
    }
    return 0;
}
#ifndef INTELLECT_HW64_HOST_TEST
/* Validate BootInfo map bounds before dereferencing the physical range list. */
static int firmware_span(const intellect_bootinfo_t *boot,
                         uint64_t address, size_t length) {
    if (!boot || boot->memory_map_count==0u ||
        boot->memory_map_count>5300u ||
        boot->memory_map_stride!=sizeof(intellect_memory_range_t) ||
        boot->memory_map_addr==0u ||
        boot->memory_map_addr>PHYS_LIMIT-((uint64_t)boot->memory_map_count*24u))
        return 0;
    const intellect_memory_range_t *r=
        (const intellect_memory_range_t *)(uintptr_t)boot->memory_map_addr;
    return intellect_hw64_range_allowed(r,boot->memory_map_count,address,
                                       length,boot->acpi_rsdp_addr);
}
static const uint8_t *firmware_table(const intellect_bootinfo_t *boot,
                                    uint64_t pa, unsigned min_len,
                                    unsigned maximum, uint32_t *total) {
    if (!firmware_span(boot,pa,min_len)) return NULL;
    const uint8_t *p=(const uint8_t *)(uintptr_t)pa;
    uint32_t n=le32(p+4u);
    if (n<min_len || n>maximum || !firmware_span(boot,pa,n)) return NULL;
    if (!intellect_hw64_sdt_validate(p,n,total)) return NULL;
    return p;
}
static void acpi_scan(const intellect_bootinfo_t *boot) {
    uint64_t pa=boot->acpi_rsdp_addr;
    if (!(boot->flags&INTELLECT_FLAG_ACPI_RSDP_FOUND) ||
        !firmware_span(boot,pa,20u)) return;
    const uint8_t *p=(const uint8_t *)(uintptr_t)pa;
    uint32_t len=p[15]>=2u ? 36u : 20u;
    if (!firmware_span(boot,pa,len)) return;
    if (len==36u) {
        len=le32(p+20u);
        if (len>RSDP_MAX || !firmware_span(boot,pa,len)) return;
    }
    uint64_t root=0;int xsdt=0;
    if (!intellect_hw64_rsdp_parse(p,len,&root,&xsdt)) return;
    inventory.acpi_rsdp_valid=1u;
    inventory.acpi_revision=p[15];
    uint32_t size=0;
    const uint8_t *table=firmware_table(boot,root,36u,SDT_MAX,&size);
    if (!table || !equal(table,xsdt?"XSDT":"RSDT",4u)) return;
    const uint32_t stride=xsdt?8u:4u;
    if ((size-36u)%stride) return;
    inventory.acpi_root_valid=1u;
    unsigned n=(size-36u)/stride;
    /* Don't execute AML or follow external pointers unboundedly. */
    for (unsigned i=0;i<n;++i) {
        uint64_t child=xsdt?le64(table+36u+i*8u):le32(table+36u+i*4u);
        uint32_t child_size=0;
        const uint8_t *sub=firmware_table(boot,child,36u,SDT_MAX,&child_size);
        if (!sub) continue;
        if (inventory.acpi_count>=INTELLECT_HW64_MAX_ACPI) {
            inventory.acpi_truncated=1u;continue;
        }
        char *dest=inventory.acpi_table_signatures[inventory.acpi_count++];
        for (unsigned j=0;j<4u;++j) {
            uint8_t c=sub[j]; dest[j]=c>=32u && c<=126u ? (char)c : '?';
        }
        dest[4]=0;
    }
}

static uint32_t pci_conf1_read(void *ctx,uint8_t bus,uint8_t dev,uint8_t fn,uint8_t off) {
    (void)ctx;
    uint32_t addr=UINT32_C(0x80000000)|((uint32_t)bus<<16)|
                  ((uint32_t)dev<<11)|((uint32_t)fn<<8)|(off & 0xFCu);
    uint32_t data;
    __asm__ volatile("outl %0, %w1" : : "a"(addr),"Nd"((uint16_t)0xCF8) : "memory");
    __asm__ volatile("inl %w1, %0" : "=a"(data) : "Nd"((uint16_t)0xCFC) : "memory");
    return data;
}
static int pci_conf1_probe(void) {
    uint32_t old=0,seen=0;
    __asm__ volatile("inl %w1, %0" : "=a"(old) : "Nd"((uint16_t)0xCF8));
    uint32_t probe=UINT32_C(0x80000000);
    __asm__ volatile("outl %0, %w1" : : "a"(probe),"Nd"((uint16_t)0xCF8));
    __asm__ volatile("inl %w1, %0" : "=a"(seen) : "Nd"((uint16_t)0xCF8));
    __asm__ volatile("outl %0, %w1" : : "a"(old),"Nd"((uint16_t)0xCF8));
    return (seen & UINT32_C(0x80000000))!=0u;
}
static void cpu_probe(void) {
    uint32_t a,b,c,d;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0),"c"(0));
    inventory.cpu_max_leaf=a;
    const uint32_t words[3]={b,d,c};
    for (unsigned i=0;i<3u;++i)
        for(unsigned j=0;j<4u;++j)
            inventory.cpu_vendor[i*4u+j]=(char)(words[i]>>(8u*j));
    inventory.cpu_vendor[12]=0;
    if (inventory.cpu_max_leaf>=1u) {
        __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1u),"c"(0u));
        inventory.cpu_signature=a;
        inventory.cpu_features_ecx=c;
        inventory.cpu_features_edx=d;
    }
}
#else
static void cpu_probe(void) {
    const char s[]="HostFixture";
    for (unsigned i=0;i<sizeof(s);++i) inventory.cpu_vendor[i]=s[i];
}
#endif

int intellect_hw64_init(const intellect_bootinfo_t *boot) {
    if (inventory_ready || !boot || boot->magic!=INTELLECT_BOOTINFO_MAGIC ||
        boot->version!=INTELLECT_BOOTINFO_VERSION) return 0;
    /* BSS static snapshot is zeroed by the UEFI loader. */
    cpu_probe();
    inventory.memory_ranges=boot->memory_map_count;
    if (boot->memory_map_count<=5300u &&
        boot->memory_map_stride==sizeof(intellect_memory_range_t) &&
        boot->memory_map_addr &&
        boot->memory_map_addr<PHYS_LIMIT &&
        (uint64_t)boot->memory_map_count*sizeof(intellect_memory_range_t)<=
              PHYS_LIMIT-boot->memory_map_addr) {
        const intellect_memory_range_t *r=
            (const intellect_memory_range_t *)(uintptr_t)boot->memory_map_addr;
        uint64_t bytes=0;
        for (uint32_t i=0;i<boot->memory_map_count;++i) {
            if (r[i].type==INTELLECT_MEMORY_USABLE &&
                r[i].length<=UINT64_MAX-bytes) bytes+=r[i].length;
        }
        inventory.conventional_mib=bytes/1048576u;
    }
    inventory.framebuffer_addr=boot->framebuffer_addr;
    inventory.framebuffer_size=boot->framebuffer_size;
    inventory.framebuffer_ready=boot->video_kind==INTELLECT_VIDEO_FRAMEBUFFER;
    inventory.smbios_reported=(boot->flags&INTELLECT_FLAG_SMBIOS_FOUND)!=0u;
#ifndef INTELLECT_HW64_HOST_TEST
    /* Caller initialized COM1 earlier and it must respond once again. */
    extern int intellect_serial64_init(void);
    inventory.serial_ready=(uint8_t)!!intellect_serial64_init();
    if (pci_conf1_probe()) {
        uint32_t saved_cfg_addr;
        __asm__ volatile("inl %w1, %0" : "=a"(saved_cfg_addr) : "Nd"((uint16_t)0xCF8));
        inventory.pci_cfg1_ready=1u;
        int truncated=0;
        inventory.pci_count=(uint16_t)intellect_hw64_scan_pci(
            pci_conf1_read,NULL,inventory.pci,INTELLECT_HW64_MAX_PCI,&truncated);
        inventory.pci_truncated=(uint8_t)truncated;
        /* Restore shared CONFIG_ADDRESS latch for subsequent PCI users. */
        __asm__ volatile("outl %0, %w1" : : "a"(saved_cfg_addr),"Nd"((uint16_t)0xCF8));
    }
    acpi_scan(boot);
#else
    (void)boot;
#endif
    inventory_ready=1;
    return 1;
}
const intellect_hw64_snapshot_t *intellect_hw64_snapshot(void) {
    return inventory_ready?&inventory:NULL;
}
