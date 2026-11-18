/* Independent-host fixture: the mock UEFI services are installed at numeric
 * ABI offsets, NOT through efi_min.h fields. This catches wrong UEFI layouts.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "bootinfo_builder.h"

static _Alignas(4096) uint8_t snapshot_area[64u*4096u];
static _Alignas(16) uint8_t raw_bs[384u];
static efi_gop_mode_information_t video_info={0,1280,720,1,{0,0,0,0},1280};
static efi_gop_mode_t video_mode={1,0,&video_info,sizeof(video_info),0xE0000000u,1280u*720u*4u};
static efi_gop_t video_gop={NULL,NULL,NULL,&video_mode};
static char output[8192u];
static size_t output_index;
static uint8_t rsdp[36u];
static uint8_t smbios3[24u];
static size_t current_fixture=0;

static efi_status_t EFIAPI output_string(efi_simple_text_output_t *out,const efi_char16_t *str) {
    (void)out;
    while(*str!=0 && output_index+1u<sizeof(output)) {
        output[output_index++]=(char)((*str<=127u) ? *str : '?');
        ++str;
    }
    output[output_index]=0;
    return EFI_SUCCESS;
}
static efi_status_t EFIAPI allocate_pages(uint32_t policy,uint32_t type,size_t pages,uint64_t *addr) {
    if(policy!=1u || type!=2u || pages!=64u) return EFI_INVALID_PARAMETER;
    *addr=(uint64_t)(uintptr_t)snapshot_area;
    return EFI_SUCCESS;
}
static efi_status_t EFIAPI locate_protocol(const efi_guid_t *guid,void *registration,void **out) {
    (void)registration;
    if(guid->data1!=0x9042a9deu) return EFI_INVALID_PARAMETER;
    *out=&video_gop;
    return EFI_SUCCESS;
}
static efi_status_t EFIAPI get_memory_map(size_t *size,void *map,size_t *key,size_t *stride,uint32_t *version) {
    const size_t count=3u;
    if(*size < count*48u) { *size=count*48u; return EFI_BUFFER_TOO_SMALL; }
    uint8_t *out=(uint8_t *)map;
    memset(out,0,count*48u);
    efi_memory_descriptor_t d[3]={{7,0,0x100000u,0,256u,0},
                                  {6,0,0xF00000u,0,4u,UINT64_C(0x8000000000000000)},
                                  {9,0,0xC00000u,0,4u,0}};
    if(current_fixture==1u) d[1].physical_start=UINT64_MAX-4095u;
    if(current_fixture==2u) d[0].number_of_pages=0u;
    for(size_t i=0;i<count;++i) memcpy(out+i*48u,&d[i],sizeof(d[i]));
    *size=count*48u;*key=123u;*stride=48u;*version=1u;
    return EFI_SUCCESS;
}
static void correct_checksum(uint8_t *p,size_t n,size_t index) {
    unsigned int sum=0;
    p[index]=0;
    for(size_t i=0;i<n;++i)sum+=p[i];
    p[index]=(uint8_t)(0u-(uint8_t)sum);
}
static void make_firmware_tables(void) {
    memset(rsdp,0,sizeof(rsdp));
    memcpy(rsdp,"RSD PTR ",8u);
    rsdp[15]=2u;rsdp[20]=36u;
    correct_checksum(rsdp,20u,8u);
    correct_checksum(rsdp,36u,32u);
    memset(smbios3,0,sizeof(smbios3));
    memcpy(smbios3,"_SM3_",5u);
    smbios3[6]=24u;
    correct_checksum(smbios3,24u,5u);
}
int main(void) {
    make_firmware_tables();
    efi_status_t (EFIAPI *alloc_fn)(uint32_t,uint32_t,size_t,uint64_t *)=allocate_pages;
    efi_status_t (EFIAPI *map_fn)(size_t *,void *,size_t *,size_t *,uint32_t *)=get_memory_map;
    efi_status_t (EFIAPI *locate_fn)(const efi_guid_t *,void *,void **)=locate_protocol;
    memcpy(raw_bs+40u,&alloc_fn,sizeof(alloc_fn));
    memcpy(raw_bs+56u,&map_fn,sizeof(map_fn));
    memcpy(raw_bs+320u,&locate_fn,sizeof(locate_fn));
    efi_configuration_table_t tables[2]={0};
    tables[0].vendor_guid=(efi_guid_t){0x8868e871u,0xe4f1u,0x11d3u,{0xbc,0x22,0,0x80,0xc7,0x3c,0x88,0x81}};
    tables[0].vendor_table=rsdp;
    tables[1].vendor_guid=(efi_guid_t){0xf2fd1544u,0x9794u,0x4a2cu,{0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}};
    tables[1].vendor_table=smbios3;
    const efi_char16_t vendor[]={'P','h','o','e','n','i','x',0};
    efi_simple_text_output_t text={0};text.output_string=output_string;
    efi_system_table_t st={0};
    st.con_out=&text;st.firmware_vendor=(efi_char16_t *)vendor;
    st.firmware_revision=0x12345678u;st.hdr.revision=0x00020032u;
    st.configuration_table=tables;st.number_of_table_entries=2u;
    st.boot_services=(efi_boot_services_t *)(void *)raw_bs;
    intellect_uefi_bootinfo_snapshot_t snapshot={0};
    if(!intellect_uefi_build_bootinfo(&st,&snapshot)) { puts("FAIL: build"); return 1; }
    const intellect_bootinfo_t *h=snapshot.header;
    if(!intellect_uefi_validate_bootinfo(&snapshot) || h->boot_method!=INTELLECT_BOOT_UEFI ||
       h->uefi_revision!=0x00020032u || h->firmware_revision!=0x12345678u ||
       strcmp(h->firmware_vendor,"Phoenix")!=0 || h->memory_map_count!=3u ||
       h->framebuffer_width!=1280u || h->framebuffer_height!=720u ||
       h->pixel_format!=INTELLECT_PIXEL_BGRX8888 || h->framebuffer_pitch!=5120u ||
       h->memory_map_stride!=24u || h->memory_map_addr!=snapshot.physical_base+4096u ||
       snapshot.ranges[0].type!=INTELLECT_MEMORY_USABLE ||
       snapshot.ranges[1].type!=INTELLECT_MEMORY_RESERVED ||
       snapshot.ranges[2].type!=INTELLECT_MEMORY_ACPI_RECLAIM ||
       (h->flags&(INTELLECT_FLAG_MEMORY_MAP_VALID|INTELLECT_FLAG_ACPI_RSDP_FOUND|INTELLECT_FLAG_SMBIOS_FOUND))!=
       (INTELLECT_FLAG_MEMORY_MAP_VALID|INTELLECT_FLAG_ACPI_RSDP_FOUND|INTELLECT_FLAG_SMBIOS_FOUND) ||
       snapshot.usable_mib!=1u || h->acpi_rsdp_addr!=(uintptr_t)rsdp || h->smbios_addr!=(uintptr_t)smbios3) {
        puts("FAIL: normalized ABI contents");return 1;
    }
    intellect_uefi_print_bootinfo(&st,&snapshot);
    if(strstr(output,"VALIDATED PRE-EXIT")==NULL || strstr(output,"Normalized map  : 3 ranges")==NULL) {
        puts("FAIL: status output");return 1;
    }
    /* Negative tests must fail closed. */
    h=(const intellect_bootinfo_t *)snapshot.header;
    snapshot.header->memory_map_stride=48u;
    if(intellect_uefi_validate_bootinfo(&snapshot)) {puts("FAIL: malformed stride");return 1;}
    snapshot.header->memory_map_stride=24u;
    current_fixture=1u;
    if(intellect_uefi_build_bootinfo(&st,&snapshot)) {puts("FAIL: overflow descriptor");return 1;}
    current_fixture=2u;
    if(intellect_uefi_build_bootinfo(&st,&snapshot)) {puts("FAIL: zero pages descriptor");return 1;}
    puts("PASS: Stage D independent UEFI ABI +40/+56/+320, normalized BootInfo v1, GOP, valid ACPI/SMBIOS, fail-closed map");
    return 0;
}
