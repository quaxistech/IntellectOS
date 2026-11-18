/* Intellect OS UEFI Stage E: native x86-64 kernel handoff.
 * After a successful ExitBootServices, execution never calls UEFI again.
 */
#include "efi_min.h"
#include "firmware_info.h"
#include "platform_probe.h"
#include "bootinfo_builder.h"
#include "framebuffer_console.h"
#include "handoff.h"

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
    efi_simple_text_output_t *out=st->con_out;
    (void)out->output_string(out,relocation_anchor);
    uefi_output_ascii(out,"Preparing freestanding x86-64 kernel handoff.\r\n");
    uefi_print_firmware_info(st);
    uefi_probe_platform(st);
    intellect_uefi_bootinfo_snapshot_t snapshot;
    if(!intellect_uefi_build_bootinfo(st,&snapshot)) {
        uefi_output_ascii(out,"BootInfo staging FAILED: no handoff.\r\n");
        return EFI_INVALID_PARAMETER;
    }
    intellect_uefi_print_bootinfo(st,&snapshot);
    if(!intellect_uefi_draw_pre_exit(&snapshot)) {
        uefi_output_ascii(out,"Framebuffer unsuitable: handoff blocked.\r\n");
        return EFI_INVALID_PARAMETER;
    }
    /* All critical allocations and final map refresh happen in handoff.
     * It must not call firmware after a successful ExitBootServices(). */
    intellect_uefi_handoff(image_handle,st,&snapshot);
}
