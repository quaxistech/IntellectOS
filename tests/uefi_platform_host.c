/* Freestanding UEFI platform probe under deterministic host-provided mocks. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "platform_probe.h"

static char transcript[8192];
static size_t transcript_length;
static efi_gop_mode_information_t gop_info = {0, 1280, 720, 1, {0,0,0,0}, 1280};
static efi_gop_mode_t gop_mode = {3,1,&gop_info,sizeof(gop_info),0xE0000000u,1280u*720u*4u};
static efi_gop_t gop = {NULL,NULL,NULL,&gop_mode};

static efi_status_t EFIAPI output_string(efi_simple_text_output_t *self, const efi_char16_t *text) {
    (void)self;
    while (*text != 0 && transcript_length+1 < sizeof(transcript)) {
        transcript[transcript_length++] = (*text <= 0x7f) ? (char)*text : '?';
        ++text;
    }
    transcript[transcript_length] = 0;
    return EFI_SUCCESS;
}

static efi_status_t EFIAPI locate_protocol(const efi_guid_t *guid, void *registration, void **out) {
    (void)registration;
    if (guid->data1 == 0x9042a9deu && guid->data2 == 0x23dcu && guid->data3 == 0x4a38u) {
        *out=&gop;
        return EFI_SUCCESS;
    }
    return EFI_INVALID_PARAMETER;
}

static efi_status_t EFIAPI get_memory_map(size_t *length, void *buffer, size_t *key,
                                          size_t *stride, uint32_t *version) {
    if (*length < 96u) {
        *length=96u;
        return EFI_BUFFER_TOO_SMALL;
    }
    uint8_t *data = (uint8_t *)buffer;
    for (size_t i=0; i<96u; ++i) data[i]=0;
    efi_memory_descriptor_t first={7,0,0x100000u,0,1024u,0};
    efi_memory_descriptor_t second={13,0,0x500000u,0,512u,0};
    __builtin_memcpy(data,&first,sizeof(first));
    __builtin_memcpy(data+48u,&second,sizeof(second));
    *length=96u;
    *key=123u;
    *stride=48u;
    *version=1u;
    return EFI_SUCCESS;
}

int main(void) {
    efi_simple_text_output_t out = {0};
    out.output_string=output_string;
    /* Independent x86-64 UEFI ABI fixture: install function pointers at
     * specification-defined byte positions, NOT through the header under test.
     * This catches self-consistent but wrong field layouts in efi_min.h. */
    _Alignas(16) unsigned char raw_bs[384]={0};
    efi_status_t (EFIAPI *memory_map_fn)(size_t *, void *, size_t *, size_t *, uint32_t *)=get_memory_map;
    efi_status_t (EFIAPI *locate_fn)(const efi_guid_t *, void *, void **)=locate_protocol;
    __builtin_memcpy(raw_bs+56, &memory_map_fn, sizeof(memory_map_fn));
    __builtin_memcpy(raw_bs+320, &locate_fn, sizeof(locate_fn));
    /* Deliberately leave slot +312 NULL (LocateHandleBuffer) so that calling
     * the wrong slot reliably fails this regression test. */
    efi_configuration_table_t entries[2]={0};
    entries[0].vendor_guid=(efi_guid_t){0x8868e871u,0xe4f1u,0x11d3u,
        {0xbc,0x22,0,0x80,0xc7,0x3c,0x88,0x81}};
    entries[0].vendor_table=(void *)(uintptr_t)0x100000u;
    entries[1].vendor_guid=(efi_guid_t){0xf2fd1544u,0x9794u,0x4a2cu,
        {0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}};
    entries[1].vendor_table=(void *)(uintptr_t)0x200000u;
    efi_system_table_t st={0};
    st.con_out=&out;
    st.boot_services=(efi_boot_services_t *)(void *)raw_bs;
    st.configuration_table=entries;
    st.number_of_table_entries=2u;
    uefi_probe_platform(&st);
    const char *required[]={
        "1280 x 720", "Pixels/scanline : 1280", "BGRX 8:8:8:8",
        "0x00000000E0000000", "ACPI 2+ @ 0x0000000000100000",
        "SMBIOS 3 @ 0x0000000000200000", "Descriptors     : 2 (stride 48, version 1)",
        "Conventional RAM: 4 MiB"
    };
    for (size_t i=0; i<sizeof(required)/sizeof(required[0]); ++i) {
        if (strstr(transcript,required[i])==NULL) {
            fprintf(stderr,"Missing: %s\nTranscript:\n%s\n",required[i],transcript);
            return 1;
        }
    }
    puts("PASS: spec-indexed UEFI BootServices +320 LocateProtocol, GOP, ACPI, SMBIOS, and variable memory-map stride");
    return 0;
}
