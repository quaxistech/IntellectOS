#ifndef INTELLECT_EFI_MIN_H
#define INTELLECT_EFI_MIN_H

/* Minimal x86-64 UEFI System Table subset for Stages A/B/C/D/E.
 * Layout follows UEFI 2.11; no external runtime or C standard library.
 * Firmware tables and BootServices use the Microsoft x64 ABI.
 */
#include <stddef.h>
#include <stdint.h>

typedef uint64_t efi_status_t;
typedef void *efi_handle_t;
typedef uint16_t efi_char16_t;
#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS UINT64_C(0)
#define EFI_INVALID_PARAMETER (UINT64_C(0x8000000000000000) | 2u)
#define EFI_ERROR(st) (((st) & UINT64_C(0x8000000000000000)) != 0u)
#define EFI_BUFFER_TOO_SMALL (UINT64_C(0x8000000000000000) | 5u)
#define EFI_SYSTEM_TABLE_SIGNATURE UINT64_C(0x5453595320494249)

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header_t;

typedef struct efi_simple_text_output {
    efi_status_t (EFIAPI *reset)(struct efi_simple_text_output *, uint8_t);
    efi_status_t (EFIAPI *output_string)(struct efi_simple_text_output *, const efi_char16_t *);
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *set_attribute;
    void *clear_screen;
    void *set_cursor_position;
    void *enable_cursor;
    void *mode;
} efi_simple_text_output_t;

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} efi_guid_t;

typedef struct {
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

typedef struct {
    efi_guid_t vendor_guid;
    void *vendor_table;
} efi_configuration_table_t;

typedef struct {
    efi_table_header_t hdr;
    void *raise_tpl;                         /* +24 */
    void *restore_tpl;                       /* +32 */
    efi_status_t (EFIAPI *allocate_pages)(uint32_t, uint32_t, size_t, uint64_t *); /* +40 */
    void *free_pages;                        /* +48 */
    efi_status_t (EFIAPI *get_memory_map)(size_t *, void *, size_t *, size_t *, uint32_t *); /* +56 */
    void *functions_before_exit[21];         /* +64 .. +224 */
    efi_status_t (EFIAPI *exit_boot_services)(efi_handle_t, size_t); /* +232 */
    void *get_next_monotonic_count;          /* +240 */
    efi_status_t (EFIAPI *stall)(size_t microseconds); /* +248 */
    /* UEFI Boot Services slots +256..+312, as specified by UEFI 2.11:
     * SetWatchdogTimer, ConnectController, DisconnectController,
     * OpenProtocol, CloseProtocol, OpenProtocolInformation,
     * ProtocolsPerHandle, LocateHandleBuffer.
     * LocateProtocol is the NEXT slot, at +320 on x86-64.
     */
    void *functions_before_locate[8];       /* +256 .. +312 */
    efi_status_t (EFIAPI *locate_protocol)(const efi_guid_t *, void *, void **); /* +320 */
} efi_boot_services_t;

typedef struct {
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} efi_pixel_bitmask_t;

typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    efi_pixel_bitmask_t pixel_information;
    uint32_t pixels_per_scan_line;
} efi_gop_mode_information_t;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    efi_gop_mode_information_t *info;
    size_t size_of_info;
    uint64_t frame_buffer_base;
    size_t frame_buffer_size;
} efi_gop_mode_t;

typedef struct efi_gop {
    void *query_mode;
    void *set_mode;
    void *blt;
    efi_gop_mode_t *mode;
} efi_gop_t;

typedef struct {
    efi_table_header_t hdr;
    efi_char16_t *firmware_vendor;
    uint32_t firmware_revision;
    uint32_t padding;
    efi_handle_t console_in_handle;
    void *con_in;
    efi_handle_t console_out_handle;
    efi_simple_text_output_t *con_out;
    efi_handle_t standard_error_handle;
    efi_simple_text_output_t *std_err;
    void *runtime_services;
    efi_boot_services_t *boot_services;
    size_t number_of_table_entries;
    efi_configuration_table_t *configuration_table;
} efi_system_table_t;

_Static_assert(sizeof(efi_table_header_t) == 24, "EFI header size");
_Static_assert(offsetof(efi_simple_text_output_t, output_string) == 8, "EFI ConOut output_string offset");
_Static_assert(offsetof(efi_boot_services_t, allocate_pages) == 40, "EFI BootServices AllocatePages offset");
_Static_assert(offsetof(efi_boot_services_t, get_memory_map) == 56, "EFI BootServices GetMemoryMap offset");
_Static_assert(offsetof(efi_boot_services_t, exit_boot_services) == 232, "EFI BootServices ExitBootServices offset");
_Static_assert(offsetof(efi_boot_services_t, stall) == 248, "EFI BootServices Stall offset");
_Static_assert(offsetof(efi_boot_services_t, locate_protocol) == 320, "EFI BootServices LocateProtocol offset");
_Static_assert(sizeof(efi_memory_descriptor_t) == 40, "EFI MemoryDescriptor size");
_Static_assert(sizeof(efi_gop_mode_information_t) == 36, "EFI GOP mode information size");
_Static_assert(sizeof(efi_configuration_table_t) == 24, "EFI configuration table entry size");
_Static_assert(offsetof(efi_system_table_t, con_out) == 64, "EFI SystemTable ConOut offset");
_Static_assert(offsetof(efi_system_table_t, boot_services) == 96, "EFI SystemTable BootServices offset");
_Static_assert(sizeof(efi_system_table_t) == 120, "EFI SystemTable size");

#endif
