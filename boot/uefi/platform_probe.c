/* Intellect OS UEFI Stage C: read-only discovery, no ExitBootServices.
 * Reads GOP, GetMemoryMap and SystemTable.ConfigurationTable.
 * Does not switch graphics modes, allocate pages, install drivers or write disks.
 */
#include "platform_probe.h"
#include "firmware_info.h"

static const efi_guid_t gop_guid = {
    0x9042a9deu, 0x23dcu, 0x4a38u, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};
static const efi_guid_t acpi2_guid = {
    0x8868e871u, 0xe4f1u, 0x11d3u, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}
};
static const efi_guid_t acpi1_guid = {
    0xeb9d2d30u, 0x2d88u, 0x11d3u, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}
};
static const efi_guid_t smbios3_guid = {
    0xf2fd1544u, 0x9794u, 0x4a2cu, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94}
};
static const efi_guid_t smbios2_guid = {
    0xeb9d2d31u, 0x2d88u, 0x11d3u, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}
};

static int guid_equal(const efi_guid_t *a, const efi_guid_t *b) {
    if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3) return 0;
    for (size_t i=0; i<8u; ++i) if (a->data4[i] != b->data4[i]) return 0;
    return 1;
}

static void print_u64(efi_simple_text_output_t *out, uint64_t v) {
    char reverse[21]; char forward[21]; size_t n=0, p=0;
    do { reverse[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v != 0u);
    while (n) forward[p++] = reverse[--n];
    forward[p] = 0;
    uefi_output_ascii(out, forward);
}

static void print_hex64(efi_simple_text_output_t *out, uint64_t v) {
    static const char dig[] = "0123456789ABCDEF";
    char s[19] = "0x0000000000000000";
    for (size_t i=0; i<16u; ++i) { s[17u-i] = dig[v & 15u]; v >>= 4u; }
    uefi_output_ascii(out,s);
}

static void print_gop(const efi_system_table_t *st) {
    efi_simple_text_output_t *out=st->con_out;
    uefi_output_ascii(out,"\r\n[GOP Graphics Output]\r\n");
    const efi_boot_services_t *bs=st->boot_services;
    if (bs == NULL || bs->locate_protocol == NULL) { uefi_output_ascii(out,"GOP: BootServices unavailable\r\n"); return; }
    efi_gop_t *gop=NULL;
    efi_status_t status=bs->locate_protocol(&gop_guid,NULL,(void **)&gop);
    if (EFI_ERROR(status) || gop == NULL || gop->mode == NULL || gop->mode->info == NULL ||
        gop->mode->size_of_info < sizeof(efi_gop_mode_information_t)) {
        uefi_output_ascii(out,"GOP: not available (no direct framebuffer information)\r\n");
        return;
    }
    const efi_gop_mode_t *mode=gop->mode;
    const efi_gop_mode_information_t *info=mode->info;
    uefi_output_ascii(out,"Resolution      : ");
    uefi_output_u32(out,info->horizontal_resolution);
    uefi_output_ascii(out," x "); uefi_output_u32(out,info->vertical_resolution);
    uefi_output_ascii(out,"\r\nPixels/scanline : "); uefi_output_u32(out,info->pixels_per_scan_line);
    uefi_output_ascii(out,"\r\nPixel format    : ");
    if (info->pixel_format==0u) uefi_output_ascii(out,"RGBX 8:8:8:8");
    else if (info->pixel_format==1u) uefi_output_ascii(out,"BGRX 8:8:8:8");
    else if (info->pixel_format==2u) uefi_output_ascii(out,"BitMask");
    else if (info->pixel_format==3u) uefi_output_ascii(out,"BltOnly (no direct framebuffer)");
    else uefi_output_ascii(out,"Unknown");
    uefi_output_ascii(out,"\r\nFramebuffer    : ");
    if (info->pixel_format==3u || mode->frame_buffer_size==0u) {
        uefi_output_ascii(out,"not directly accessible\r\n");
    } else {
        print_hex64(out,mode->frame_buffer_base);
        uefi_output_ascii(out," (bytes: "); print_u64(out,(uint64_t)mode->frame_buffer_size);
        uefi_output_ascii(out,")\r\n");
    }
}

/* Static reserved buffer belongs to our loaded EFI image; it is not an
 * independently allocated region. Stage D must allocate/copy durable data
 * before ExitBootServices, using the final memory map and map key. */
#define MEMORY_MAP_BUFFER_BYTES (128u*1024u)
static uint64_t map_storage[MEMORY_MAP_BUFFER_BYTES/sizeof(uint64_t)];

static void print_memory_map(const efi_system_table_t *st) {
    efi_simple_text_output_t *out=st->con_out;
    uefi_output_ascii(out,"\r\n[UEFI Memory Map]\r\n");
    const efi_boot_services_t *bs=st->boot_services;
    if (bs==NULL || bs->get_memory_map==NULL) { uefi_output_ascii(out,"GetMemoryMap: unavailable\r\n"); return; }
    size_t used_size=sizeof(map_storage);
    size_t key=0, descriptor_size=0;
    uint32_t descriptor_version=0;
    efi_status_t status=bs->get_memory_map(&used_size,map_storage,&key,&descriptor_size,&descriptor_version);
    if (status==EFI_BUFFER_TOO_SMALL) {
        uefi_output_ascii(out,"GetMemoryMap: buffer too small, needed bytes: "); print_u64(out,(uint64_t)used_size);
        uefi_output_ascii(out,"\r\n"); return;
    }
    if (EFI_ERROR(status) || descriptor_size<sizeof(efi_memory_descriptor_t) ||
        used_size>sizeof(map_storage) || used_size%descriptor_size!=0u) {
        uefi_output_ascii(out,"GetMemoryMap: error or invalid descriptor layout\r\n"); return;
    }
    size_t count=used_size/descriptor_size;
    uint64_t conventional_pages=0;
    uint32_t conventional_ranges=0;
    const uint8_t *buffer=(const uint8_t *)map_storage;
    for (size_t i=0; i<count; ++i) {
        const efi_memory_descriptor_t *d=(const efi_memory_descriptor_t *)(const void *)(buffer + i*descriptor_size);
        if (d->type==7u) {
            if (UINT64_MAX - conventional_pages < d->number_of_pages) {
                uefi_output_ascii(out,"RAM: accounting overflow\r\n"); return;
            }
            conventional_pages+=d->number_of_pages;
            ++conventional_ranges;
        }
    }
    uefi_output_ascii(out,"Descriptors     : "); print_u64(out,(uint64_t)count);
    uefi_output_ascii(out," (stride "); print_u64(out,(uint64_t)descriptor_size);
    uefi_output_ascii(out,", version "); uefi_output_u32(out,descriptor_version);
    uefi_output_ascii(out,")\r\nConventional RAM: "); print_u64(out,conventional_pages/256u);
    uefi_output_ascii(out," MiB (truncated, "); uefi_output_u32(out,conventional_ranges);
    uefi_output_ascii(out," regions)\r\n");
    uefi_output_ascii(out,"NOTE: UEFI owns memory until ExitBootServices; RAM above is not installed capacity.\r\n");
    (void)key;
}

static void print_configuration_tables(const efi_system_table_t *st) {
    efi_simple_text_output_t *out=st->con_out;
    uefi_output_ascii(out,"\r\n[Configuration Tables]\r\n");
    /* Do not follow pointers here: only locate the address and table identity. */
    if (st->configuration_table==NULL || st->number_of_table_entries>1024u) {
        uefi_output_ascii(out,"Table list: missing or unreasonable entry count\r\n"); return;
    }
    void *acpi2=NULL, *acpi1=NULL, *smbios3=NULL, *smbios2=NULL;
    for (size_t i=0; i<st->number_of_table_entries; ++i) {
        const efi_configuration_table_t *entry=&st->configuration_table[i];
        if (guid_equal(&entry->vendor_guid,&acpi2_guid)) acpi2=entry->vendor_table;
        else if (guid_equal(&entry->vendor_guid,&acpi1_guid)) acpi1=entry->vendor_table;
        else if (guid_equal(&entry->vendor_guid,&smbios3_guid)) smbios3=entry->vendor_table;
        else if (guid_equal(&entry->vendor_guid,&smbios2_guid)) smbios2=entry->vendor_table;
    }
    uefi_output_ascii(out,"ACPI RSDP       : ");
    if (acpi2 != NULL) { uefi_output_ascii(out,"ACPI 2+ @ "); print_hex64(out,(uint64_t)(uintptr_t)acpi2); }
    else if (acpi1 != NULL) { uefi_output_ascii(out,"ACPI 1.0 @ "); print_hex64(out,(uint64_t)(uintptr_t)acpi1); }
    else uefi_output_ascii(out,"not found");
    uefi_output_ascii(out,"\r\nSMBIOS          : ");
    if (smbios3 != NULL) { uefi_output_ascii(out,"SMBIOS 3 @ "); print_hex64(out,(uint64_t)(uintptr_t)smbios3); }
    else if (smbios2 != NULL) { uefi_output_ascii(out,"SMBIOS 2 @ "); print_hex64(out,(uint64_t)(uintptr_t)smbios2); }
    else uefi_output_ascii(out,"not found");
    uefi_output_ascii(out,"\r\n");
}

void uefi_probe_platform(const efi_system_table_t *st) {
    print_gop(st);
    print_configuration_tables(st);
    print_memory_map(st); /* perform last: callbacks/console may alter memory map */
}
