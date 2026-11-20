/* Read-only H1/H2 serial diagnostic commands. No dynamic allocation. */
#include <stddef.h>
#include <stdint.h>
#include "hwdiag64.h"
#include "driver64.h"

typedef struct { intellect_hwdiag64_emit_t fn; void *ctx; } writer_t;
static void ch(writer_t *w,char c) {w->fn(w->ctx,c);}
static void str(writer_t *w,const char *s) {while(*s) ch(w,*s++);}
static void num(writer_t *w,uint64_t n) {
    char buf[21];unsigned i=0;
    do {buf[i++]=(char)('0'+n%10u);n/=10u;} while(n);
    while(i) ch(w,buf[--i]);
}
static void hex(writer_t *w,uint32_t v,unsigned digits) {
    static const char hexdigits[]="0123456789ABCDEF";
    for (unsigned i=digits;i;--i) ch(w,hexdigits[(v>>((i-1u)*4u))&15u]);
}
static int eq(const char *a,const char *b) {
    while(*a && *b && *a==*b) {++a;++b;}
    return *a==*b;
}
static void pcidev(writer_t *w,const intellect_hw64_pci_t *p) {
    str(w,"PCI ");hex(w,p->bus,2u);ch(w,':');hex(w,p->device,2u);
    ch(w,'.');num(w,p->function);
    str(w," ID=");hex(w,p->vendor_id,4u);ch(w,':');hex(w,p->device_id,4u);
    str(w," CLASS=");hex(w,p->class_code,2u);hex(w,p->subclass,2u);
    hex(w,p->prog_if,2u);
    str(w," ");str(w,intellect_driver64_pci_class(p));
    str(w," DRIVER=");str(w,intellect_driver64_pci_requirement(p));
    if(p->class_code==6u && p->subclass==4u) {
        str(w," CHILD_BUS=");num(w,p->secondary_bus);
    }
    ch(w,'\n');
}
int intellect_hwdiag64_command(const char *cmd,
                              intellect_hwdiag64_emit_t emit, void *ctx) {
    if (!cmd || !emit) return 0;
    writer_t w={emit,ctx};
    const intellect_hw64_snapshot_t *s=intellect_hw64_snapshot();
    if (!s) {str(&w,"H1: HARDWARE INVENTORY NOT READY\n");return 1;}
    if (eq(cmd,"hw") || eq(cmd,"hw help")) {
        str(&w,"HW COMMANDS: hw summary | hw cpu | hw acpi | hw pci | hw list | hw tree | hw drivers\n");
        str(&w,"READ-ONLY: no device controllers or disks modified\n");return 1;
    }
    if (eq(cmd,"hw summary") || eq(cmd,"hw list")) {
        str(&w,"H1 INVENTORY: CPU=");str(&w,s->cpu_vendor);
        str(&w," PCI=");num(&w,s->pci_count);
        str(&w," PCI_TRUNCATED=");num(&w,s->pci_truncated);
        str(&w," ACPI_ROOT=");str(&w,s->acpi_root_valid?"VALID":"UNAVAILABLE");
        str(&w," ACPI_TABLES=");num(&w,s->acpi_count);
        str(&w," MEMORY_RANGES=");num(&w,s->memory_ranges);
        str(&w," CONVENTIONAL_MIB=");num(&w,s->conventional_mib);
        str(&w," UART=");str(&w,s->serial_ready?"READY":"UNAVAILABLE");
        str(&w," FB=");str(&w,s->framebuffer_ready?"READY":"UNAVAILABLE");ch(&w,'\n');
        if (eq(cmd,"hw summary")) return 1;
        for (unsigned i=0;i<s->pci_count;++i) pcidev(&w,&s->pci[i]);
        return 1;
    }
    if (eq(cmd,"hw cpu")) {
        str(&w,"CPU VENDOR=");str(&w,s->cpu_vendor);
        str(&w," CPUID_MAX_BASIC=");num(&w,s->cpu_max_leaf);
        str(&w," SIGNATURE=0x");hex(&w,s->cpu_signature,8u);
        str(&w," FEATURES_ECX=0x");hex(&w,s->cpu_features_ecx,8u);
        str(&w," FEATURES_EDX=0x");hex(&w,s->cpu_features_edx,8u);
        ch(&w,'\n');return 1;
    }
    if (eq(cmd,"hw acpi")) {
        str(&w,"ACPI RSDP=");str(&w,s->acpi_rsdp_valid?"VALID":"INVALID_OR_ABSENT");
        str(&w," REVISION=");num(&w,s->acpi_revision);
        str(&w," ROOT=");str(&w,s->acpi_root_valid?"VALID":"INVALID_OR_ABSENT");
        str(&w," TABLES=");num(&w,s->acpi_count);
        str(&w," TRUNCATED=");num(&w,s->acpi_truncated);
        str(&w," SMBIOS_REPORTED=");num(&w,s->smbios_reported);ch(&w,'\n');
        for (unsigned i=0;i<s->acpi_count;++i) {
            str(&w,"ACPI TABLE ");str(&w,s->acpi_table_signatures[i]);ch(&w,'\n');
        }
        return 1;
    }
    if (eq(cmd,"hw pci")) {
        str(&w,"PCI CONF1=");str(&w,s->pci_cfg1_ready?"READY":"NOT_SUPPORTED");
        str(&w," DEVICES=");num(&w,s->pci_count);
        str(&w," TRUNCATED=");num(&w,s->pci_truncated);ch(&w,'\n');
        for (unsigned i=0;i<s->pci_count;++i) pcidev(&w,&s->pci[i]);
        return 1;
    }
    if (eq(cmd,"hw tree")) {
        str(&w,"HOST\n  + CPU: ");str(&w,s->cpu_vendor);
        str(&w,"\n  + PLATFORM: UART / GOP framebuffer / ACPI\n");
        str(&w,"  + PCI segment 0 (config mechanism #1, flat bus inventory)\n");
        for (unsigned i=0;i<s->pci_count;++i) {str(&w,"    + ");pcidev(&w,&s->pci[i]);}
        str(&w,"NOTE: inventory, not ACPI namespace topology\n");return 1;
    }
    if (eq(cmd,"hw drivers")) {
        str(&w,"H2 BUILTIN DRIVER REGISTRY (limited PIIX ATA read-only support)\n");
        for (unsigned i=1;i<=INTELLECT_DRIVER64_COUNT;++i) {
            const intellect_driver64_record_t *r=intellect_driver64_get(i);
            str(&w,"DRIVER ");str(&w,intellect_driver64_name(i));
            str(&w," STATE=");str(&w,r && r->state?"ACTIVE":"UNAVAILABLE");
            str(&w," ATTACHED=");num(&w,r?r->attached:0);ch(&w,'\n');
        }
        return 1;
    }
    return 0;
}
