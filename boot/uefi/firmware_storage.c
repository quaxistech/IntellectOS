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

unsigned intellect_uefi_block_device_count(efi_system_table_t *st) {
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
    unsigned found=0;
    if(n<=EFI_MAX_BLOCK_HANDLES) {
        for(size_t i=0;i<n;++i) {
            efi_block_io_t *block=NULL;
            if(EFI_ERROR(handle.fn(handles[i],&block_guid,(void **)&block)) ||
               !block || !block->media) continue;
            const efi_block_media_t *m=block->media;
            if (m->media_present && !m->logical_partition &&
                m->block_size>=512u && m->block_size<=65536u &&
                (m->block_size & (m->block_size-1u))==0u) ++found;
        }
    }
    (void)release.fn(handles);
    return found;
}
