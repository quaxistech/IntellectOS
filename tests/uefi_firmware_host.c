/* Executed on the Ubuntu build host; tests the firmware formatting module
 * with a synthetic EFI System Table, not real firmware. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "efi_min.h"
#include "firmware_info.h"

static char captured[4096];
static size_t used;

static efi_status_t EFIAPI mock_output(efi_simple_text_output_t *unused,
                                       const efi_char16_t *text) {
    (void)unused;
    while (*text) {
        if (used + 1u >= sizeof(captured)) return EFI_INVALID_PARAMETER;
        efi_char16_t ch = *text++;
        captured[used++] = ch < 0x80u ? (char)ch : '?';
    }
    captured[used] = 0;
    return EFI_SUCCESS;
}

static int verify(uint32_t revision, uint32_t firmware_revision, const efi_char16_t *vendor,
                  const char *expected_revision, const char *expected_vendor,
                  const char *expected_fw_revision) {
    efi_simple_text_output_t out = {0};
    out.output_string = mock_output;
    efi_system_table_t st = {0};
    st.hdr.revision = revision;
    st.hdr.header_size = sizeof(st);
    st.firmware_revision = firmware_revision;
    st.firmware_vendor = (efi_char16_t *)vendor;
    st.con_out = &out;
    used = 0;
    captured[0] = 0;
    uefi_print_firmware_info(&st);
    if (!strstr(captured, expected_revision) || !strstr(captured, expected_vendor) ||
        !strstr(captured, expected_fw_revision)) {
        fprintf(stderr, "Firmware test failed:\n%s\n", captured);
        return 1;
    }
    return 0;
}

int main(void) {
    static const efi_char16_t ovmf[] = {'E','D','K',' ','I','I',0};
    if (verify(0x00020046u, 0x1234u, ovmf, "UEFI interface revision : 2.70",
               "Firmware vendor         : EDK II", "0x00001234 (decimal 4660)")) return 1;
    if (verify(0x0002006Eu, 0u, NULL, "UEFI interface revision : 2.110",
               "<not provided>", "0x00000000 (decimal 0)")) return 1;
    puts("PASS: host UEFI firmware formatting (2.70, 2.110, vendor, raw OEM revision)");
    return 0;
}
