#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hardware64.h"
#include "driver64.h"
#include "hwdiag64.h"

static void seal(uint8_t *p,size_t n,size_t checksum_off) {
    p[checksum_off]=0;
    unsigned sum=0;
    for(size_t i=0;i<n;++i)sum+=p[i];
    p[checksum_off]=(uint8_t)(0u-sum);
}
static void set32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(n>>(8u*i));
}
static uint32_t fixture_pci(void *ctx,uint8_t bus,uint8_t dev,uint8_t fn,uint8_t off) {
    (void)ctx;
    if(bus==0u && dev==1u && fn==0u) {
        if(off==0u)return 0x10001AF4u;
        if(off==8u)return 0x02000001u;
        if(off==12u)return 0x00000000u;
    }
    if(bus==0u && dev==2u && fn==0u) {
        if(off==0u)return 0x00018086u;
        if(off==8u)return 0x06040001u;
        if(off==12u)return 0x00800000u; /* multifunction */
        if(off==24u)return 0x00020200u;
    }
    if(bus==0u && dev==2u && fn==1u) {
        if(off==0u)return 0x29228086u;
        if(off==8u)return 0x01060101u; /* AHCI */
        if(off==12u)return 0u;
    }
    if(bus==3u && dev==5u && fn==0u) {
        if(off==0u)return 0xA808144Du;
        if(off==8u)return 0x01080200u; /* NVMe */
        if(off==12u)return 0u;
    }
    return 0xFFFFFFFFu;
}
static char output[2048];
static size_t outlen;
static void emit(void *ctx,char c) {
    (void)ctx;
    if(outlen+1<sizeof(output)) output[outlen++]=c;
    output[outlen]=0;
}
static void test_parsers(void) {
    uint8_t rsdp[36]={0};
    memcpy(rsdp,"RSD PTR ",8u);
    rsdp[15]=2u;
    set32(rsdp+16,0x110000u);
    set32(rsdp+20,36u);
    set32(rsdp+24,0x12345000u);
    seal(rsdp,20u,8u);
    seal(rsdp,36u,32u);
    uint64_t root=0;int x=0;
    assert(intellect_hw64_rsdp_parse(rsdp,sizeof(rsdp),&root,&x));
    assert(root==0x12345000u && x==1);
    rsdp[10]^=1u;
    assert(!intellect_hw64_rsdp_parse(rsdp,sizeof(rsdp),&root,&x));
    rsdp[10]^=1u;
    rsdp[32]^=1u;
    assert(!intellect_hw64_rsdp_parse(rsdp,sizeof(rsdp),&root,&x));
    rsdp[32]^=1u;
    assert(!intellect_hw64_rsdp_parse(rsdp,35u,&root,&x));
    rsdp[15]=0u;
    seal(rsdp,20u,8u);
    assert(intellect_hw64_rsdp_parse(rsdp,20u,&root,&x));
    assert(root==0x110000u && x==0);
    uint8_t sdt[44]={0};
    memcpy(sdt,"XSDT",4u);
    set32(sdt+4,44u);
    seal(sdt,sizeof(sdt),9u);
    uint32_t n=0;
    assert(intellect_hw64_sdt_validate(sdt,sizeof(sdt),&n) && n==44u);
    sdt[11]^=4u;
    assert(!intellect_hw64_sdt_validate(sdt,sizeof(sdt),&n));
    sdt[11]^=4u;
    assert(!intellect_hw64_sdt_validate(sdt,43u,&n));
    set32(sdt+4,UINT32_MAX);
    assert(!intellect_hw64_sdt_validate(sdt,sizeof(sdt),&n));
}
static void test_memory_spans(void) {
    intellect_memory_range_t r[]={
        {0x1000u,0x2000u,INTELLECT_MEMORY_RESERVED,0u},
        {0x4000u,0x4000u,INTELLECT_MEMORY_ACPI_RECLAIM,0u},
        {0x8000u,0x1000u,INTELLECT_MEMORY_USABLE,0u}
    };
    assert(intellect_hw64_range_allowed(r,3u,0x1000u,20u,0x1000u));
    assert(!intellect_hw64_range_allowed(r,3u,0x1000u,20u,0x1400u));
    assert(intellect_hw64_range_allowed(r,3u,0x4000u,36u,0x1000u));
    assert(!intellect_hw64_range_allowed(r,3u,0x7FF0u,32u,0x1000u));
    assert(!intellect_hw64_range_allowed(r,3u,0x8000u,36u,0x1000u));
    assert(!intellect_hw64_range_allowed(r,3u,UINT64_MAX-2u,8u,0x1000u));
    assert(!intellect_hw64_range_allowed(r,3u,0x1000u,0u,0x1000u));
}
static void test_pci(void) {
    intellect_hw64_pci_t list[8]={0};int truncated=-1;
    size_t count=intellect_hw64_scan_pci(fixture_pci,NULL,list,8u,&truncated);
    assert(count==4u && !truncated);
    assert(list[0].vendor_id==0x1AF4 && list[0].class_code==2u);
    assert(list[1].class_code==6u && list[1].secondary_bus==2u);
    assert(list[2].function==1u && list[2].subclass==6u);
    assert(list[3].bus==3u && list[3].device==5u && list[3].subclass==8u);
    assert(!strcmp(intellect_driver64_pci_class(&list[3]),"NVMe"));
    assert(!strcmp(intellect_driver64_pci_requirement(&list[3]),"nvme-driver-needed"));
    assert(intellect_hw64_scan_pci(fixture_pci,NULL,list,2u,&truncated)==2u && truncated);
    assert(intellect_hw64_scan_pci(NULL,NULL,list,8u,&truncated)==0u);
}
static void test_driver_state(void) {
    intellect_hw64_snapshot_t fake={0};
    fake.serial_ready=1u;
    fake.framebuffer_ready=1u;
    fake.acpi_root_valid=1u;
    fake.pci_cfg1_ready=1u;
    fake.framebuffer_addr=0x80000000u;
    fake.framebuffer_size=4096000u;
    assert(intellect_driver64_init(&fake));
    assert(!intellect_driver64_init(&fake));
    for(unsigned id=1;id<=INTELLECT_DRIVER64_ACPI_TABLES;++id) {
        const intellect_driver64_record_t *r=intellect_driver64_get(id);
        assert(r && r->id==id && r->state==INTELLECT_DRIVER64_ACTIVE &&
               r->attached==1u && r->capabilities);
        assert(strlen(intellect_driver64_name(id))>5u);
    }
    assert(intellect_driver64_get(0)==NULL);
    assert(intellect_driver64_get(6)==NULL);
    assert(intellect_driver64_claim_count()==3u);
    assert(intellect_driver64_get(INTELLECT_DRIVER64_ATA_PIO)->state==
           INTELLECT_DRIVER64_UNAVAILABLE);
    assert(!intellect_driver64_claim(INTELLECT_DRIVER64_PCI_CONFIG,
                                    INTELLECT_DRIVER64_PIO,0x3FCu,4u));
    assert(!intellect_driver64_claim(INTELLECT_DRIVER64_PCI_CONFIG,
                                    INTELLECT_DRIVER64_PIO,UINT64_MAX-4u,16u));
    assert(!intellect_driver64_claim(0u,INTELLECT_DRIVER64_PIO,0x42u,1u));
    assert(intellect_driver64_claim(INTELLECT_DRIVER64_PCI_CONFIG,
                                    INTELLECT_DRIVER64_PIO,0x400u,8u));
    assert(intellect_driver64_claim_count()==4u);
    /* Host-only snapshot to smoke command printer (never accesses x86 I/O). */
    intellect_bootinfo_t boot={0};boot.magic=INTELLECT_BOOTINFO_MAGIC;
    boot.version=INTELLECT_BOOTINFO_VERSION;
    boot.memory_map_count=42u;
    assert(intellect_hw64_init(&boot));
    assert(!intellect_hw64_init(&boot));
    assert(intellect_hwdiag64_command("hw summary",emit,NULL));
    assert(strstr(output,"H1 INVENTORY: CPU=HostFixture"));
    outlen=0;output[0]=0;
    assert(intellect_hwdiag64_command("hw drivers",emit,NULL));
    assert(strstr(output,"H2 BUILTIN DRIVER REGISTRY"));
    assert(!intellect_hwdiag64_command("hw nonsense",emit,NULL));
}
int main(void) {
    test_parsers();test_memory_spans();test_pci();test_driver_state();
    puts("PASS: H1/H2 ACPI RSDP + SDT, PCI scan, driver states, diagnostics");
    return 0;
}
