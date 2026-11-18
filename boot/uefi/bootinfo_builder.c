/* Intellect BootInfo v1 — UEFI Stage D normalized pre-ExitBootServices snapshot.
 * Nothing in this module calls ExitBootServices or hands control to a kernel.
 * Validity of BootInfo header does NOT mean the map is final for a kernel.
 */
#include "bootinfo_builder.h"
#include "firmware_info.h"

#define UEFI_LOADER_DATA 2u
#define EFI_ALLOCATE_MAX_ADDRESS 1u
#define SNAPSHOT_PAGES 64u
#define SNAPSHOT_BYTES (SNAPSHOT_PAGES * 4096u)
#define NORMALIZED_OFFSET 4096u
#define RAW_OFFSET 131072u
#define RAW_CAPACITY (SNAPSHOT_BYTES - RAW_OFFSET)
#define MAX_NORMALIZED ((RAW_OFFSET - NORMALIZED_OFFSET) / sizeof(intellect_memory_range_t))

static const efi_guid_t guid_gop = {0x9042a9deu,0x23dcu,0x4a38u,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static const efi_guid_t guid_acpi2 = {0x8868e871u,0xe4f1u,0x11d3u,{0xbc,0x22,0,0x80,0xc7,0x3c,0x88,0x81}};
static const efi_guid_t guid_acpi1 = {0xeb9d2d30u,0x2d88u,0x11d3u,{0x9a,0x16,0,0x90,0x27,0x3f,0xc1,0x4d}};
static const efi_guid_t guid_smbios3 = {0xf2fd1544u,0x9794u,0x4a2cu,{0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}};
static const efi_guid_t guid_smbios2 = {0xeb9d2d31u,0x2d88u,0x11d3u,{0x9a,0x16,0,0x90,0x27,0x3f,0xc1,0x4d}};

static void zero_bytes(void *p, size_t n) {
    uint8_t *b=(uint8_t *)p;
    while (n--) *b++=0;
}
static int guid_same(const efi_guid_t *a,const efi_guid_t *b) {
    if (a->data1!=b->data1 || a->data2!=b->data2 || a->data3!=b->data3) return 0;
    for (size_t i=0;i<8u;++i) if(a->data4[i]!=b->data4[i]) return 0;
    return 1;
}
static int checksum_ok(const uint8_t *p, size_t n) {
    uint8_t sum=0;
    for(size_t i=0;i<n;++i) sum=(uint8_t)(sum+p[i]);
    return sum==0;
}
static int ascii_match(const uint8_t *p,const char *str,size_t n) {
    for(size_t i=0;i<n;++i) if(p[i]!=(uint8_t)str[i]) return 0;
    return 1;
}
static uint32_t memory_class(uint32_t kind) {
    switch (kind) {
        case 7u: return INTELLECT_MEMORY_USABLE;
        case 9u: return INTELLECT_MEMORY_ACPI_RECLAIM;
        case 10u: return INTELLECT_MEMORY_ACPI_NVS;
        case 8u: return INTELLECT_MEMORY_BAD;
        default: return INTELLECT_MEMORY_RESERVED;
    }
}
static uint32_t safe_firmware_vendor(char dst[32],const efi_char16_t *src) {
    size_t i=0;
    if(src == NULL) {dst[0]=0;return 0u;}
    for (;i<31u && src[i];++i) {
        uint16_t c=src[i];
        dst[i]=(c>=32u && c<=126u) ? (char)c : '?';
    }
    dst[i]=0;
    return (uint32_t)i;
}
static void detect_tables(const efi_system_table_t *st,intellect_bootinfo_t *out) {
    if(st->configuration_table==NULL || st->number_of_table_entries>1024u) return;
    const uint8_t *a2=NULL,*a1=NULL,*s3=NULL,*s2=NULL;
    for(size_t i=0;i<st->number_of_table_entries;++i) {
        const efi_configuration_table_t *e=&st->configuration_table[i];
        if(guid_same(&e->vendor_guid,&guid_acpi2)) a2=(const uint8_t *)e->vendor_table;
        else if(guid_same(&e->vendor_guid,&guid_acpi1)) a1=(const uint8_t *)e->vendor_table;
        else if(guid_same(&e->vendor_guid,&guid_smbios3)) s3=(const uint8_t *)e->vendor_table;
        else if(guid_same(&e->vendor_guid,&guid_smbios2)) s2=(const uint8_t *)e->vendor_table;
    }
    const uint8_t *acpi=NULL;
    if(a2!=NULL && ascii_match(a2,"RSD PTR ",8u) && checksum_ok(a2,20u)) {
        /* ACPI 2+ extension length is at offset 20; avoid large/unbounded reads. */
        uint32_t length=0;
        __builtin_memcpy(&length,a2+20,4u);
        if(a2[15]>=2u && length>=36u && length<=4096u && checksum_ok(a2,length)) acpi=a2;
    }
    if(acpi==NULL && a1!=NULL && ascii_match(a1,"RSD PTR ",8u) && checksum_ok(a1,20u)) acpi=a1;
    if(acpi!=NULL) {
        out->acpi_rsdp_addr=(uint64_t)(uintptr_t)acpi;
        out->flags|=INTELLECT_FLAG_ACPI_RSDP_FOUND;
    }
    const uint8_t *smbios=NULL;
    if(s3!=NULL && ascii_match(s3,"_SM3_",5u)) {
        uint8_t length=s3[6];
        if(length>=24u && length<=64u && checksum_ok(s3,length)) smbios=s3;
    }
    if(smbios==NULL && s2!=NULL && ascii_match(s2,"_SM_",4u)) {
        uint8_t length=s2[5];
        if(length>=31u && length<=64u && ascii_match(s2+16,"_DMI_",5u) && checksum_ok(s2,length)) smbios=s2;
    }
    if(smbios!=NULL) {
        out->smbios_addr=(uint64_t)(uintptr_t)smbios;
        out->flags|=INTELLECT_FLAG_SMBIOS_FOUND;
    }
}
static void detect_video(const efi_system_table_t *st,intellect_bootinfo_t *out) {
    const efi_boot_services_t *bs=st->boot_services;
    if(bs==NULL || bs->locate_protocol==NULL) return;
    efi_gop_t *gop=NULL;
    efi_status_t status=bs->locate_protocol(&guid_gop,NULL,(void **)&gop);
    if(EFI_ERROR(status) || gop==NULL || gop->mode==NULL || gop->mode->info==NULL ||
       gop->mode->size_of_info < sizeof(efi_gop_mode_information_t)) return;
    const efi_gop_mode_information_t *info=gop->mode->info;
    const efi_gop_mode_t *mode=gop->mode;
    if(info->horizontal_resolution==0u || info->vertical_resolution==0u ||
       info->pixels_per_scan_line<info->horizontal_resolution ||
       info->pixels_per_scan_line>UINT32_MAX/4u || info->pixel_format>2u) return;
    uint32_t pitch=info->pixels_per_scan_line*4u;
    uint64_t span=(uint64_t)pitch*info->vertical_resolution;
    if(mode->frame_buffer_base==0u || mode->frame_buffer_size<span ||
       mode->frame_buffer_base>UINT64_MAX-(uint64_t)mode->frame_buffer_size) return;
    out->video_kind=INTELLECT_VIDEO_FRAMEBUFFER;
    out->framebuffer_addr=mode->frame_buffer_base;
    out->framebuffer_size=(uint64_t)mode->frame_buffer_size;
    out->framebuffer_width=info->horizontal_resolution;
    out->framebuffer_height=info->vertical_resolution;
    out->framebuffer_pitch=pitch;
    out->pixel_format=info->pixel_format==0u ? INTELLECT_PIXEL_RGBX8888:
                      info->pixel_format==1u ? INTELLECT_PIXEL_BGRX8888: INTELLECT_PIXEL_BITMASK;
    if(info->pixel_format==2u) {
        out->pixel_red_mask=info->pixel_information.red_mask;
        out->pixel_green_mask=info->pixel_information.green_mask;
        out->pixel_blue_mask=info->pixel_information.blue_mask;
        out->pixel_reserved_mask=info->pixel_information.reserved_mask;
    }
}

int intellect_uefi_build_bootinfo(const efi_system_table_t *st,
                                 intellect_uefi_bootinfo_snapshot_t *result) {
    if(st==NULL || result==NULL || st->boot_services==NULL) return 0;
    zero_bytes(result,sizeof(*result));
    const efi_boot_services_t *bs=st->boot_services;
    if(bs->allocate_pages==NULL || bs->get_memory_map==NULL) return 0;
    /* Try below 4 GiB for the future i386 consumer. Fallback is diagnostic only. */
    uint64_t allocation=UINT64_C(0xFFFFFFFF);
    efi_status_t status=bs->allocate_pages(EFI_ALLOCATE_MAX_ADDRESS,UEFI_LOADER_DATA,SNAPSHOT_PAGES,&allocation);
    if(EFI_ERROR(status)) {
        allocation=0u;
        status=bs->allocate_pages(0u,UEFI_LOADER_DATA,SNAPSHOT_PAGES,&allocation);
        if(EFI_ERROR(status)) return 0;
    }
    if(allocation==0u || allocation>UINT64_MAX-SNAPSHOT_BYTES) return 0;
    uint8_t *base=(uint8_t *)(uintptr_t)allocation;
    zero_bytes(base,SNAPSHOT_BYTES);
    intellect_bootinfo_t *header=(intellect_bootinfo_t *)(void *)base;
    intellect_memory_range_t *ranges=(intellect_memory_range_t *)(void *)(base+NORMALIZED_OFFSET);
    uint8_t *raw=base+RAW_OFFSET;
    header->magic=INTELLECT_BOOTINFO_MAGIC;
    header->version=INTELLECT_BOOTINFO_VERSION;
    header->header_size=(uint16_t)sizeof(*header);
    header->total_size=(uint32_t)sizeof(*header);
    header->boot_method=INTELLECT_BOOT_UEFI;
    header->boot_drive=UINT32_MAX;
    header->uefi_revision=st->hdr.revision;
    header->firmware_revision=st->firmware_revision;
    header->memory_map_stride=(uint32_t)sizeof(intellect_memory_range_t);
    header->memory_map_addr=allocation+NORMALIZED_OFFSET;
    (void)safe_firmware_vendor(header->firmware_vendor,st->firmware_vendor);
    detect_video(st,header);
    detect_tables(st,header);

    size_t bytes=RAW_CAPACITY, key=0, stride=0;
    uint32_t version=0;
    status=bs->get_memory_map(&bytes,raw,&key,&stride,&version);
    if(EFI_ERROR(status) || version==0u || stride<sizeof(efi_memory_descriptor_t) ||
       stride>RAW_CAPACITY || bytes>RAW_CAPACITY || bytes%stride!=0u) return 0;
    size_t count=bytes/stride;
    if(count>MAX_NORMALIZED || count>UINT32_MAX) {
        header->flags|=INTELLECT_FLAG_MEMORY_MAP_TRUNC;
        return 0; /* Never pass a partial memory map as valid. */
    }
    uint64_t conventional_pages=0;
    uint32_t conventional_ranges=0;
    for(size_t i=0;i<count;++i) {
        efi_memory_descriptor_t d;
        __builtin_memcpy(&d,raw+i*stride,sizeof(d));
        if(d.number_of_pages==0u || d.number_of_pages>UINT64_MAX/4096u ||
           (d.physical_start&4095u)!=0u) return 0;
        const uint64_t size=d.number_of_pages*4096u;
        if(d.physical_start>UINT64_MAX-size) return 0;
        intellect_memory_range_t *dst=&ranges[i];
        dst->base=d.physical_start;
        dst->length=size;
        dst->type=memory_class(d.type);
        /* ABI v1 stores only low 32-bit attribute flags. Runtime types remain
         * RESERVED; EFI_MEMORY_RUNTIME lives in the high bit and is not encoded.
         * Full UEFI memory attributes will require an explicit ABI extension.
         */
        dst->attributes=(uint32_t)d.attribute;
        if(d.type==7u) {
            if(conventional_pages>UINT64_MAX-d.number_of_pages) return 0;
            conventional_pages+=d.number_of_pages;
            ++conventional_ranges;
        }
    }
    header->memory_map_count=(uint32_t)count;
    header->flags|=INTELLECT_FLAG_MEMORY_MAP_VALID;
    result->header=header;
    result->ranges=ranges;
    result->physical_base=allocation;
    result->descriptor_count=(uint32_t)count;
    result->descriptor_stride=(uint32_t)stride;
    result->descriptor_version=version;
    result->normalized_count=(uint32_t)count;
    result->usable_count=conventional_ranges;
    result->usable_mib=conventional_pages/256u;
    result->address_below_4g=(uint8_t)(allocation+SNAPSHOT_BYTES<=UINT64_C(0x100000000));
    (void)key; /* Not valid for a future ExitBootServices after further UEFI calls. */
    return intellect_uefi_validate_bootinfo(result);
}

int intellect_uefi_validate_bootinfo(const intellect_uefi_bootinfo_snapshot_t *snapshot) {
    if(snapshot==NULL || snapshot->header==NULL || snapshot->ranges==NULL) return 0;
    const intellect_bootinfo_t *h=snapshot->header;
    if(h->magic!=INTELLECT_BOOTINFO_MAGIC || h->version!=INTELLECT_BOOTINFO_VERSION ||
       h->header_size!=sizeof(*h) || h->total_size!=sizeof(*h) ||
       h->boot_method!=INTELLECT_BOOT_UEFI || h->boot_drive!=UINT32_MAX ||
       (h->flags&INTELLECT_FLAG_MEMORY_MAP_VALID)==0u ||
       (h->flags&INTELLECT_FLAG_MEMORY_MAP_TRUNC)!=0u ||
       h->memory_map_stride!=sizeof(intellect_memory_range_t) ||
       h->memory_map_count!=snapshot->normalized_count ||
       h->memory_map_addr!=snapshot->physical_base+NORMALIZED_OFFSET ||
       snapshot->normalized_count==0u || snapshot->normalized_count>MAX_NORMALIZED ||
       snapshot->address_below_4g>1u) return 0;
    if(h->video_kind==INTELLECT_VIDEO_FRAMEBUFFER) {
        if(h->framebuffer_addr==0u || h->framebuffer_width==0u || h->framebuffer_height==0u ||
           h->framebuffer_pitch<4u*h->framebuffer_width || h->pixel_format==INTELLECT_PIXEL_UNKNOWN) return 0;
    }
    for(size_t i=0;i<h->memory_map_count;++i) {
        const intellect_memory_range_t *r=&snapshot->ranges[i];
        if(r->length==0u || r->base>UINT64_MAX-r->length ||
           r->type<INTELLECT_MEMORY_USABLE || r->type>INTELLECT_MEMORY_BAD) return 0;
    }
    return h->firmware_vendor[31]=='\0';
}

/* E2: Normalization is performed after the final GetMemoryMap, BEFORE
 * ExitBootServices. No allocations, logs, or UEFI calls occur here.
 * A failure is fail-closed: never hand a partial map to a kernel.
 */
void *intellect_uefi_final_map_buffer(const intellect_uefi_bootinfo_snapshot_t *snapshot) {
    if(snapshot==NULL || snapshot->physical_base==0u) return NULL;
    return (void *)(uintptr_t)(snapshot->physical_base + RAW_OFFSET);
}
size_t intellect_uefi_final_map_capacity(void) { return RAW_CAPACITY; }
int intellect_uefi_refresh_bootinfo_map(intellect_uefi_bootinfo_snapshot_t *snapshot,
                                        size_t bytes, size_t stride, uint32_t version) {
    if(snapshot==NULL || snapshot->header==NULL || snapshot->ranges==NULL ||
       snapshot->physical_base==0u || version==0u ||
       stride<sizeof(efi_memory_descriptor_t) || stride>RAW_CAPACITY ||
       bytes>RAW_CAPACITY || bytes==0u || bytes%stride!=0u) return 0;
    size_t count=bytes/stride;
    if(count>MAX_NORMALIZED || count>UINT32_MAX) return 0;
    const uint8_t *raw=(const uint8_t *)intellect_uefi_final_map_buffer(snapshot);
    intellect_memory_range_t *dest=(intellect_memory_range_t *)(void *)snapshot->ranges;
    uint64_t pages=0;
    uint32_t usable_ranges=0;
    for(size_t i=0;i<count;++i) {
        efi_memory_descriptor_t d;
        __builtin_memcpy(&d,raw+i*stride,sizeof(d));
        if(d.number_of_pages==0u || d.number_of_pages>UINT64_MAX/4096u ||
           (d.physical_start&4095u)!=0u ||
           d.physical_start>UINT64_MAX-d.number_of_pages*4096u) return 0;
        dest[i].base=d.physical_start;
        dest[i].length=d.number_of_pages*4096u;
        dest[i].type=memory_class(d.type);
        dest[i].attributes=(uint32_t)d.attribute;
        if(d.type==7u) {
            if(pages>UINT64_MAX-d.number_of_pages) return 0;
            pages+=d.number_of_pages;
            ++usable_ranges;
        }
    }
    snapshot->header->memory_map_count=(uint32_t)count;
    snapshot->header->flags|=INTELLECT_FLAG_MEMORY_MAP_VALID;
    snapshot->header->flags &= ~INTELLECT_FLAG_MEMORY_MAP_TRUNC;
    snapshot->descriptor_count=(uint32_t)count;
    snapshot->descriptor_stride=(uint32_t)stride;
    snapshot->descriptor_version=version;
    snapshot->normalized_count=(uint32_t)count;
    snapshot->usable_count=usable_ranges;
    snapshot->usable_mib=pages/256u;
    return intellect_uefi_validate_bootinfo(snapshot);
}

static void output_u64(efi_simple_text_output_t *out,uint64_t v) {
    char backwards[21],digits[21];size_t n=0,p=0;
    do{backwards[n++]=(char)('0'+v%10u);v/=10u;}while(v);
    while(n) {digits[p++]=backwards[--n];} digits[p]=0;
    uefi_output_ascii(out,digits);
}
static void output_hex64(efi_simple_text_output_t *out,uint64_t v) {
    char s[19]="0x0000000000000000";static const char hex[]="0123456789ABCDEF";
    for(size_t i=0;i<16u;++i){s[17u-i]=hex[v&15u];v>>=4u;}
    uefi_output_ascii(out,s);
}
void intellect_uefi_print_bootinfo(const efi_system_table_t *st,
                                  const intellect_uefi_bootinfo_snapshot_t *snapshot) {
    efi_simple_text_output_t *out=st->con_out;
    uefi_output_ascii(out,"\r\n[Intellect BootInfo v1 - UEFI snapshot]\r\n");
    if(!intellect_uefi_validate_bootinfo(snapshot)) {
        uefi_output_ascii(out,"Status: INVALID (no kernel handoff)\r\n"); return;
    }
    const intellect_bootinfo_t *h=snapshot->header;
    uefi_output_ascii(out,"Status          : VALIDATED PRE-EXIT (not a final handoff)\r\n");
    uefi_output_ascii(out,"Header          : ");output_hex64(out,snapshot->physical_base);
    uefi_output_ascii(out," (152 bytes)\r\nMemory array    : ");output_hex64(out,h->memory_map_addr);
    uefi_output_ascii(out," (24-byte entries)\r\nNormalized map  : ");uefi_output_u32(out,h->memory_map_count);
    uefi_output_ascii(out," ranges, conventional ");output_u64(out,snapshot->usable_mib);
    uefi_output_ascii(out," MiB\r\nFirmware        : ");uefi_output_ascii(out,h->firmware_vendor);
    uefi_output_ascii(out," / UEFI rev ");uefi_output_hex32(out,h->uefi_revision);
    uefi_output_ascii(out,"\r\nVideo           : ");
    if(h->video_kind==INTELLECT_VIDEO_FRAMEBUFFER) {
        uefi_output_u32(out,h->framebuffer_width);uefi_output_ascii(out," x ");
        uefi_output_u32(out,h->framebuffer_height);uefi_output_ascii(out," framebuffer\r\n");
    } else uefi_output_ascii(out,"no direct GOP framebuffer\r\n");
    uefi_output_ascii(out,"ACPI / SMBIOS   : ");
    uefi_output_ascii(out,(h->flags&INTELLECT_FLAG_ACPI_RSDP_FOUND)?"valid":"not validated");
    uefi_output_ascii(out," / ");
    uefi_output_ascii(out,(h->flags&INTELLECT_FLAG_SMBIOS_FOUND)?"valid":"not validated");
    uefi_output_ascii(out,"\r\nBelow 4 GiB    : ");
    uefi_output_ascii(out,snapshot->address_below_4g?"yes":"no (i386 handoff blocked)");
    uefi_output_ascii(out,"\r\nNo ExitBootServices; BootInfo snapshot may become stale.\r\n");
}
