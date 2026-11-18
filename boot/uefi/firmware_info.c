/* UEFI Stage B firmware identification, no allocations or disk writes.
 * All firmware fields are reported verbatim, without guessing a BIOS version.
 * Read-only firmware API calls use the Microsoft x64 calling convention.
 */
#include "firmware_info.h"

#define OUT_MAX_CHARS 96u
#define VENDOR_MAX_CHARS 64u

void uefi_output_ascii(efi_simple_text_output_t *out, const char *s) {
    efi_char16_t buf[OUT_MAX_CHARS];
    size_t n = 0;
    while (*s) {
        if (n + 1u == OUT_MAX_CHARS) {
            buf[n] = 0;
            (void)out->output_string(out, buf);
            n = 0;
        }
        buf[n++] = (efi_char16_t)(unsigned char)*s++;
    }
    buf[n] = 0;
    (void)out->output_string(out, buf);
}

void uefi_output_u32(efi_simple_text_output_t *out, uint32_t value) {
    char buf[11];
    size_t n = 0;
    do {
        buf[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0);
    char normal[11];
    size_t i = 0;
    while (n != 0) normal[i++] = buf[--n];
    normal[i] = '\0';
    uefi_output_ascii(out, normal);
}

void uefi_output_hex32(efi_simple_text_output_t *out, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[11] = "0x00000000";
    for (size_t i = 0; i < 8u; ++i) {
        text[9u - i] = digits[value & 15u];
        value >>= 4u;
    }
    uefi_output_ascii(out, text);
}

static void output_vendor(efi_simple_text_output_t *out, const efi_char16_t *vendor) {
    if (vendor == NULL) {
        uefi_output_ascii(out, "<not provided>");
        return;
    }
    /* Bound the read in case firmware supplies an unterminated string.
     * Do not assume the value is ASCII: OutputString accepts UTF-16.
     */
    efi_char16_t fragment[VENDOR_MAX_CHARS + 1u];
    size_t n = 0;
    while (n < VENDOR_MAX_CHARS && vendor[n] != 0) {
        efi_char16_t ch = vendor[n];
        fragment[n] = (ch < 0x20u || ch == 0x7Fu) ? (efi_char16_t)'?' : ch;
        ++n;
    }
    fragment[n] = 0;
    (void)out->output_string(out, fragment);
    if (n == VENDOR_MAX_CHARS) uefi_output_ascii(out, " [truncated]");
}

void uefi_print_firmware_info(const efi_system_table_t *st) {
    efi_simple_text_output_t *out = st->con_out;
    const uint32_t rev = st->hdr.revision;
    uefi_output_ascii(out, "UEFI interface revision : ");
    uefi_output_u32(out, rev >> 16u);
    uefi_output_ascii(out, ".");
    uefi_output_u32(out, rev & UINT32_C(0xFFFF));
    uefi_output_ascii(out, " (raw ");
    uefi_output_hex32(out, rev);
    uefi_output_ascii(out, ")\r\n");

    uefi_output_ascii(out, "Firmware vendor         : ");
    output_vendor(out, st->firmware_vendor);
    uefi_output_ascii(out, "\r\n");

    uefi_output_ascii(out, "Firmware revision (OEM) : ");
    uefi_output_hex32(out, st->firmware_revision);
    uefi_output_ascii(out, " (decimal ");
    uefi_output_u32(out, st->firmware_revision);
    uefi_output_ascii(out, ")\r\n");
}
