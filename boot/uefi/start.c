/* Intellect OS UEFI Stage E: native x86-64 kernel handoff.
 * After a successful ExitBootServices, execution never calls UEFI again.
 */
#include "efi_min.h"
#include "firmware_info.h"
#include "platform_probe.h"
#include "bootinfo_builder.h"
#include "framebuffer_console.h"
#include "handoff.h"
#include "bootmenu.h"
#include "firmware_storage.h"

static const efi_char16_t banner[] = {
    'I','N','T','E','L','L','E','C','T',' ','O','S',' ','-',' ','U','E','F','I',' ','S','T','A','G','E',' ','E','\r','\n',0
};
/* EFI image retains an absolute relocation; OVMF can load it anywhere. */
static const efi_char16_t *volatile relocation_anchor = banner;

efi_status_t EFIAPI efi_main(efi_handle_t image_handle,efi_system_table_t *st) {
    if(st==NULL || st->hdr.signature!=EFI_SYSTEM_TABLE_SIGNATURE ||
       st->hdr.header_size < sizeof(efi_system_table_t) || st->con_out==NULL ||
       st->con_out->output_string==NULL || st->boot_services==NULL)
        return EFI_INVALID_PARAMETER;
    /* Retain one relocation in the EFI image without exposing the old Stage E
     * diagnostic banner to the user. The firmware may relocate BOOTX64.EFI. */
    (void)relocation_anchor;
    efi_simple_text_output_t *out=st->con_out;
    intellect_uefi_bootinfo_snapshot_t snapshot;
    if(!intellect_uefi_build_bootinfo(st,&snapshot)) {
        uefi_output_ascii(out,"INTELLECT OS: firmware/memory initialization failed.\r\n");
        return EFI_INVALID_PARAMETER;
    }
    /* The stock UEFI keyboard/USB/block drivers are available only now.
     * None of their protocol pointers are passed to the native kernel. */
    unsigned blocks=intellect_uefi_block_device_count(st);
    if(!intellect_uefi_bootmenu(snapshot.header,
             (intellect_efi_input_t *)st->con_in,st->boot_services,blocks)) {
        uefi_output_ascii(out,"INTELLECT OS: boot menu input/display unavailable.\r\n");
        return EFI_INVALID_PARAMETER;
    }
    /* All critical allocations and final map refresh happen in handoff.
     * It must not call firmware after a successful ExitBootServices(). */
    intellect_uefi_handoff(image_handle,st,&snapshot);
}
