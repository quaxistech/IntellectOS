/* EFI Block I/O enumeration mock: validates the ABI bridge and FreePool.
 * No calls to ReadBlocks or WriteBlocks; none implemented in this fixture.
 */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "firmware_storage.h"

typedef struct {uint32_t id;uint8_t removable,present,logical,readonly,cache;
                uint8_t padding[3];uint32_t sector,align;uint64_t last;} media_t;
typedef struct {uint64_t revision;media_t *media;void *functions[4];} block_t;
static media_t medias[3];
static block_t blocks[3];
static efi_handle_t handles[3];
static unsigned frees;
static efi_status_t EFIAPI locate(uint32_t search,const efi_guid_t *guid,void *key,
                                 size_t *count,efi_handle_t **array){
    (void)key;assert(search==2u);assert(guid && guid->data1==0x964e5b21u);
    *count=3u;*array=handles;return 0;
}
static efi_status_t EFIAPI handle(efi_handle_t h,const efi_guid_t *guid,void **out){
    (void)guid;
    for(size_t i=0;i<3u;++i){
        if(h==handles[i]){*out=&blocks[i];return 0;}
    }
    return UINT64_C(0x8000000000000002);
}
static efi_status_t EFIAPI free_uefi(void *ptr){
    assert(ptr==handles);++frees;return 0;
}
int main(void){
    _Static_assert(offsetof(media_t,last)==24u,"media layout");
    efi_boot_services_t bs;memset(&bs,0,sizeof(bs));
    efi_system_table_t st;memset(&st,0,sizeof(st));st.boot_services=&bs;
    for(unsigned i=0;i<3u;++i){
        handles[i]=(void *)(uintptr_t)(i+1u);
        blocks[i].media=&medias[i];
        medias[i].present=1u;medias[i].sector=512u;medias[i].last=1000u;
    }
    medias[2].logical=1u;
    union { void *raw; efi_status_t(EFIAPI *fn)(uint32_t,const efi_guid_t*,void*,size_t*,efi_handle_t**); } l;
    union { void *raw; efi_status_t(EFIAPI *fn)(efi_handle_t,const efi_guid_t*,void**); } h;
    union { void *raw; efi_status_t(EFIAPI *fn)(void*); } f;
    l.fn=locate;h.fn=handle;f.fn=free_uefi;
    bs.functions_before_locate[7]=l.raw;
    bs.functions_before_exit[11]=h.raw;
    bs.functions_before_exit[1]=f.raw;
    assert(intellect_uefi_block_device_count(&st)==2u);
    assert(frees==1u);
    assert(intellect_uefi_block_device_count(NULL)==0u);
    puts("PASS: UEFI Block I/O enumeration, partition filtering, FreePool");
    return 0;
}
