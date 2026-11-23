# Intellect BootInfo ABI v1 — canonical contract

`kernel/include/bootinfo.h` is the canonical C declaration. `boot/bios/stage1.S`
contains explicit absolute offsets: tests in `tests/test_bootinfo.py` guard
critical cross-language fields. Multi-byte integers are little-endian.
The header is **152 bytes**; memory entries are stored separately, each 24 bytes.
All addresses are **physical addresses** (`uint64_t`), never C pointers in the
wire format. The loader owns the memory until the kernel adopts or copies it.

## Header field map

| Offset | Type | Name | Meaning |
|---:|---|---|---|
| 0 | u32 | magic | `0x49534F33` |
| 4 | u16 | version | `1` |
| 6 | u16 | header_size | `152` |
| 8 | u32 | total_size | `152` (map separate) |
| 12 | u32 | boot_method | `1` BIOS, `2` UEFI |
| 16 | u32 | flags | Memory-map validity, truncation, ACPI/SMBIOS discovery |
| 20 | u32 | boot_drive | BIOS DL, UEFI uses `0xFFFFFFFF` |
| 24 | u32 | firmware_revision | UEFI `FirmwareRevision`; BIOS unknown=0 |
| 28 | u32 | uefi_revision | UEFI `SystemTable.Hdr.Revision`; BIOS=0 |
| 32 | u32 | video_kind | 1 VGA text, 2 linear framebuffer, 0 none |
| 36 | u32 | bios_video_mode | BIOS video mode; UEFI=0 |
| 40 | u64 | framebuffer_addr | Physical framebuffer base or zero |
| 48 | u64 | framebuffer_size | Mapped framebuffer length in bytes |
| 56 | u32 | framebuffer_width | Pixel width |
| 60 | u32 | framebuffer_height | Pixel height |
| 64 | u32 | framebuffer_pitch | Bytes per scanline, not pixel count |
| 68 | u32 | pixel_format | RGBX8888, BGRX8888 or explicit masks |
| 72 | u64 | memory_map_addr | Physical array address |
| 80 | u32 | memory_map_count | Number of entries |
| 84 | u32 | memory_map_stride | `24` |
| 88 | u64 | acpi_rsdp_addr | Optional checked RSDP physical address |
| 96 | u64 | smbios_addr | Optional SMBIOS entry point |
| 104–119 | 4×u32 | pixel masks | Red, green, blue, reserved masks |
| 120–151 | char[32] | firmware_vendor | NUL-terminated vendor name if known |

A memory entry is `[base:u64, length:u64, type:u32, attributes:u32]`. The
normalized types are 1 usable, 2 reserved, 3 ACPI reclaimable, 4 ACPI NVS,
5 bad. Unsupported BIOS E820 types are mapped to reserved. The UEFI
adapter must normalize UEFI descriptors into these types and preserve relevant
raw flags in `attributes`; it must never pass raw UEFI memory-type numbers as
if they were the normalized enum.

## BIOS implementation

Header physical address `0x5000`, array address `0x6000`, maximum 64 ranges.
Stage1 sets firmware revision, UEFI revision and vendor to zero. It records the
boot drive and VGA mode 3. E820 entries are filtered for zero length and
invalid extended attributes. If BIOS reports more than 64 entries, the
`MEMORY_MAP_TRUNC` bit is set. No E820 entries is a hard boot failure.

Stage1 searches the first 1KiB of EBDA and BIOS ROM area for valid ACPI RSDP
checksum, and the legacy BIOS ROM area for a checked SMBIOS 2.x entry point.
Absence of these optional tables is not fatal. Neither discovery executes AML
nor changes power state. **This limited discovery is not a full ACPI/SMBIOS
parser.**

## UEFI Stage D implementation (pre-ExitBootServices only)

`boot/uefi/bootinfo_builder.c` now constructs the same 152-byte ABI header
and 24-byte normalized memory records. `AllocatePages(AllocateMaxAddress,
EfiLoaderData, 64)` allocates a 256-KiB staging block. If no allocation is
available below 4 GiB, it falls back to `AllocateAnyPages` **only for
Stage D diagnostics**. `BootInfo` and the array are located at explicit
physical offsets, not a pointer into the EFI image's transient .data.

UEFI memory types are normalized conservatively: EfiConventionalMemory (7)
becomes USABLE, EfiACPIReclaimMemory (9) becomes ACPI_RECLAIM, EfiACPIMemoryNVS
(10) becomes ACPI_NVS, EfiUnusableMemory (8) becomes BAD; every other type,
including EfiLoaderData, EfiBootServices*, EfiRuntimeServices* and unknown
future types, is RESERVED. Boot-services pages are **not marked usable before
ExitBootServices**. The map buffer is checked for bounded size, descriptor
stride, descriptor lengths, page alignment and address overflow. A short or
partial map is a failure, not a successful but silently truncated snapshot.

UEFI memory attributes are 64-bit, but ABI v1 only provides a 32-bit
`attributes` slot. Stage D preserves the low 32 bits and always classifies
runtime-services regions as RESERVED; it does **not** claim to represent high
bits such as `EFI_MEMORY_RUNTIME`. A future full runtime-services design must preserve the complete
UEFI descriptor information separately or explicitly extend this ABI. Do not
use Stage D's partial attribute field as an authoritative runtime-memory map.

ACPI RSDP 1.0 checks its 20-byte checksum; ACPI 2+ checks both the 20-byte and
extended length-limited checksums. SMBIOS 2/3 entry-point signatures,
bounded lengths and checksums are validated before setting flags. The entry
points remain firmware-owned references, not deep-copied ACPI/SMBIOS tables.
UTF-16 firmware vendor text is converted into a bounded printable ASCII subset
with replacement for unsupported characters (not a full UTF-8 encoder).

GOP must supply a non-BltOnly 32-bit linear framebuffer with valid geometry,
pitch and byte bounds; otherwise BootInfo correctly records video_kind=NONE.

**Crucial lifetime boundary:** Historical Stage D deliberately did NOT call
`ExitBootServices` or launch a kernel. Its snapshot can become stale after
firmware activity. Stage E reserves the kernel and BootInfo staging block,
fetches a **fresh final** memory map/MapKey before `ExitBootServices()`, and
hands off to a distinct native x86-64 kernel. The existing 32-bit BIOS kernel
is **not** the UEFI kernel binary.

## UEFI Stage E extension (without ABI bump)

The binary ABI remains v1 (152-byte header and 24-byte memory records). The
UEFI Stage E bootloader **refreshes** the normalized memory map immediately
before `ExitBootServices()`, then passes the same preallocated BootInfo block to
an independently running native x86-64 kernel. Memory of type LoaderCode,
LoaderData, BootServicesCode, BootServicesData and runtime types remains
conservatively RESERVED in the normalized v1 map; the loader's own allocations
must not be reissued by a future allocator. The high-bit runtime attribute is
not represented by this ABI. Do not enable EFI runtime calls until the full
runtime mapping contract has been designed. ACPI/SMBIOS entry point pointers
are not deep-copied and must not be dereferenced without a separate lifetime
check. The current kernel only prints those physical addresses.
