/* UEFI Block I/O protocol inventory. Protocol and Boot Services ABI based on
 * the UEFI specification; no Linux/FreeBSD/EDK-II implementation copied.
 * All calls occur before ExitBootServices. Never issue WriteBlocks or writes.
 */
#include "firmware_storage.h"

#define EFI_BY_PROTOCOL 2u
#define EFI_MAX_BLOCK_HANDLES 128u
static const efi_guid_t block_guid={0x964e5b21u,0x6459u,0x11d2u,
                                    {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

typedef struct {
    uint32_t media_id;
    uint8_t removable,media_present,logical_partition,read_only,write_caching;
    uint8_t reserved[3];
    uint32_t block_size;
    uint32_t io_align;
    uint64_t last_block;
} efi_block_media_t;

typedef struct {
    uint64_t revision;
    efi_block_media_t *media;
    void *reset;
    void *read_blocks;
    void *write_blocks;
    void *flush_blocks;
} efi_block_io_t;

typedef efi_status_t (EFIAPI *locate_buffer_fn)(uint32_t,const efi_guid_t *,
                                                void *,size_t *,efi_handle_t **);
typedef efi_status_t (EFIAPI *handle_protocol_fn)(efi_handle_t,const efi_guid_t *,void **);
typedef efi_status_t (EFIAPI *free_pool_fn)(void *);

unsigned intellect_uefi_block_device_inventory(efi_system_table_t *st,
                                               intellect_uefi_storage_info_t *info) {
    if (info) { info->total = 0u; info->removable = 0u; info->fixed = 0u; }
    if (!st || !st->boot_services) return 0u;
    const efi_boot_services_t *bs=st->boot_services;
    /* Existing efi_min.h represents these ABI slots as pointer-sized fields.
     * 312=LocateHandleBuffer, 152=HandleProtocol, 72=FreePool.
     * A future typed Boot Services header may replace this adapter.
     */
    _Static_assert(offsetof(efi_boot_services_t,functions_before_locate)==256u,
                   "UEFI slot base changed");
    _Static_assert(offsetof(efi_boot_services_t,functions_before_exit)==64u,
                   "UEFI slot base changed");
    union { void *raw; locate_buffer_fn fn; } locate;
    union { void *raw; handle_protocol_fn fn; } handle;
    union { void *raw; free_pool_fn fn; } release;
    locate.raw=bs->functions_before_locate[7];
    handle.raw=bs->functions_before_exit[11];
    release.raw=bs->functions_before_exit[1];
    if(!locate.fn || !handle.fn || !release.fn) return 0u;
    size_t n=0;
    efi_handle_t *handles=NULL;
    efi_status_t rc=locate.fn(EFI_BY_PROTOCOL,&block_guid,NULL,&n,&handles);
    if(EFI_ERROR(rc) || !handles) return 0u;
    unsigned found=0u, removable=0u, fixed=0u;
    if(n<=EFI_MAX_BLOCK_HANDLES) {
        for(size_t i=0;i<n;++i) {
            efi_block_io_t *block=NULL;
            if(EFI_ERROR(handle.fn(handles[i],&block_guid,(void **)&block)) ||
               !block || !block->media) continue;
            const efi_block_media_t *m=block->media;
            if (m->media_present && !m->logical_partition &&
                m->block_size>=512u && m->block_size<=65536u &&
                (m->block_size & (m->block_size-1u))==0u) {
                ++found;
                if (m->removable) ++removable;
                else ++fixed;
            }
        }
    }
    (void)release.fn(handles);
    if (info) {
        info->total = found;
        info->removable = removable;
        info->fixed = fixed;
    }
    return found;
}

unsigned intellect_uefi_block_device_count(efi_system_table_t *st) {
    intellect_uefi_storage_info_t info = {0u, 0u, 0u};
    return intellect_uefi_block_device_inventory(st, &info);
}

static const efi_guid_t loaded_image_guid = {0x5B1B31A1u, 0x9B62u, 0x11D2u,
                                             {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

typedef struct {
    uint32_t revision;
    efi_handle_t parent_handle;
    efi_system_table_t *system_table;
    efi_handle_t device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    uint32_t image_code_type;
    uint32_t image_data_type;
    void *unload;
} efi_loaded_image_t;

int intellect_uefi_is_removable_boot(efi_handle_t image_handle, efi_system_table_t *st) {
    if (!st || !st->boot_services || !image_handle) return 1;
    const efi_boot_services_t *bs = st->boot_services;
    union { void *raw; handle_protocol_fn fn; } handle;
    handle.raw = bs->functions_before_exit[11];
    if (!handle.fn) return 1;

    efi_loaded_image_t *loaded = NULL;
    efi_status_t rc = handle.fn(image_handle, &loaded_image_guid, (void **)&loaded);
    if (EFI_ERROR(rc) || !loaded || !loaded->device_handle) return 1;

    efi_block_io_t *block = NULL;
    rc = handle.fn(loaded->device_handle, &block_guid, (void **)&block);
    if (EFI_ERROR(rc) || !block || !block->media) return 1;

    return block->media->removable ? 1 : 0;
}
