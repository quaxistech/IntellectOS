#ifndef INTELLECT_BOOTINFO_H
#define INTELLECT_BOOTINFO_H

#include <stddef.h>
#include <stdint.h>

/* Intellect BootInfo ABI v1, independent of BIOS/UEFI loader implementation.
 * All multi-byte fields are little-endian. Physical addresses are uint64_t.
 * At the current i386 milestone, pointers consumed by the kernel MUST fit
 * in the identity-accessible address space below 4 GiB; no paging is enabled.
 * Bootloader owns these allocations until the kernel copies the map.
 * No pointers to firmware boot-services memory may survive handoff.
 */
#define INTELLECT_BOOTINFO_MAGIC       0x49534F33u
#define INTELLECT_BOOTINFO_VERSION     1u
#define INTELLECT_BOOTINFO_ADDRESS     0x5000u  /* BIOS implementation detail */
#define INTELLECT_MEMORY_MAP_ADDRESS   0x6000u  /* BIOS implementation detail */
#define INTELLECT_MEMORY_MAP_CAPACITY  64u      /* BIOS implementation detail */

#define INTELLECT_BOOT_BIOS 1u
#define INTELLECT_BOOT_UEFI 2u

#define INTELLECT_VIDEO_NONE 0u
#define INTELLECT_VIDEO_VGA_TEXT 1u
#define INTELLECT_VIDEO_FRAMEBUFFER 2u

#define INTELLECT_FLAG_MEMORY_MAP_VALID  (1u << 0)
#define INTELLECT_FLAG_MEMORY_MAP_TRUNC  (1u << 1)
#define INTELLECT_FLAG_ACPI_RSDP_FOUND   (1u << 2)
#define INTELLECT_FLAG_SMBIOS_FOUND      (1u << 3)

/* Type is a normalized cross-firmware memory classification, not a raw
 * firmware-specific enum. Additional UEFI types map to RESERVED initially. */
#define INTELLECT_MEMORY_USABLE        1u
#define INTELLECT_MEMORY_RESERVED      2u
#define INTELLECT_MEMORY_ACPI_RECLAIM  3u
#define INTELLECT_MEMORY_ACPI_NVS      4u
#define INTELLECT_MEMORY_BAD           5u

/* Video pixel formats for VIDEO_FRAMEBUFFER, zero otherwise. */
#define INTELLECT_PIXEL_UNKNOWN        0u
#define INTELLECT_PIXEL_RGBX8888       1u
#define INTELLECT_PIXEL_BGRX8888       2u
#define INTELLECT_PIXEL_BITMASK        3u

typedef struct __attribute__((packed)) {
    uint64_t base;            /* +0 */
    uint64_t length;          /* +8 */
    uint32_t type;            /* +16: normalized classification */
    uint32_t attributes;      /* +20: firmware flags, preserved when meaningful */
} intellect_memory_range_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;           /* +0 */
    uint16_t version;         /* +4 */
    uint16_t header_size;     /* +6: sizeof(intellect_bootinfo_t) */
    uint32_t total_size;      /* +8: header only; map is separate */
    uint32_t boot_method;     /* +12: BIOS or UEFI */
    uint32_t flags;           /* +16 */
    uint32_t boot_drive;      /* +20: BIOS DL or UINT32_MAX for UEFI */
    uint32_t firmware_revision;/* +24: UEFI firmware revision, 0 for BIOS */
    uint32_t uefi_revision;   /* +28: packed UEFI specification revision, 0 for BIOS */
    uint32_t video_kind;      /* +32 */
    uint32_t bios_video_mode; /* +36: 0 for UEFI */
    uint64_t framebuffer_addr;/* +40: 0 for VGA text */
    uint64_t framebuffer_size;/* +48 */
    uint32_t framebuffer_width; /* +56 */
    uint32_t framebuffer_height;/* +60 */
    uint32_t framebuffer_pitch; /* +64: bytes per scanline */
    uint32_t pixel_format;    /* +68 */
    uint64_t memory_map_addr; /* +72: physical pointer to range array */
    uint32_t memory_map_count;/* +80 */
    uint32_t memory_map_stride;/* +84: sizeof(intellect_memory_range_t) */
    uint64_t acpi_rsdp_addr;  /* +88: optional physical RSDP pointer */
    uint64_t smbios_addr;     /* +96: optional entry-point pointer */
    uint32_t pixel_red_mask;  /* +104: used with PIXEL_BITMASK */
    uint32_t pixel_green_mask;/* +108 */
    uint32_t pixel_blue_mask; /* +112 */
    uint32_t pixel_reserved_mask; /* +116 */
    char firmware_vendor[32]; /* +120: NUL-terminated UTF-8 subset if available */
} intellect_bootinfo_t;

_Static_assert(sizeof(intellect_memory_range_t) == 24u, "memory range ABI mismatch");
_Static_assert(sizeof(intellect_bootinfo_t) == 152u, "BootInfo size ABI mismatch");
_Static_assert(offsetof(intellect_bootinfo_t, memory_map_addr) == 72u, "BootInfo field offset mismatch");
_Static_assert(offsetof(intellect_bootinfo_t, memory_map_count) == 80u, "BootInfo field offset mismatch");
_Static_assert(offsetof(intellect_bootinfo_t, acpi_rsdp_addr) == 88u, "BootInfo field offset mismatch");
_Static_assert(offsetof(intellect_bootinfo_t, firmware_vendor) == 120u, "BootInfo field offset mismatch");

#endif
