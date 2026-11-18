#!/usr/bin/env python3
"""Create a separate UEFI test image from a COPY of the verified BIOS FAT16 image.

Pure Python; no host block-device access, mounting, dd or external FAT tools.
The BIOS boot instructions, Stage1, kernel and FAT16 VBR stay byte-identical.
Only the MBR partition type changes from 0x06 (FAT16) to 0xEF (EFI System).
"""
from __future__ import annotations

import pathlib
import struct
import sys

SECTOR = 512


def le16(b: bytes | bytearray, at: int) -> int:
    return struct.unpack_from("<H", b, at)[0]


def le32(b: bytes | bytearray, at: int) -> int:
    return struct.unpack_from("<I", b, at)[0]


def dir_entry(name: bytes, attrib: int, cluster: int, size: int = 0) -> bytes:
    if len(name) != 11 or not (0 <= cluster <= 0xFFFF):
        raise ValueError("invalid FAT16 directory entry")
    e = bytearray(32)
    e[:11] = name
    e[11] = attrib
    struct.pack_into("<H", e, 26, cluster)
    struct.pack_into("<I", e, 28, size)
    return bytes(e)


def construct(bios_image: bytes, efi_binary: bytes) -> bytes:
    d = bytearray(bios_image)
    if len(d) < 36 * 1024 * 1024 // 2 or d[510:512] != b"\x55\xaa":
        raise ValueError("missing valid BIOS source image")
    if not efi_binary or len(efi_binary) > 1024 * 1024:
        raise ValueError("UEFI image must be nonempty and <= 1 MiB")
    if efi_binary[:2] != b"MZ":
        raise ValueError("UEFI binary is not PE/COFF")
    part = 446
    if d[part] != 0x80 or d[part + 4] not in (0x06, 0x0E):
        raise ValueError("expected active BIOS FAT16 partition")
    pstart, psectors = struct.unpack_from("<II", d, part + 8)
    bpb = pstart * SECTOR
    if bpb + psectors * SECTOR > len(d) or d[bpb + 510:bpb + 512] != b"\x55\xaa":
        raise ValueError("invalid FAT16 VBR/bounds")
    spc = d[bpb + 13]
    reserved = le16(d, bpb + 14)
    nfats = d[bpb + 16]
    root_entries = le16(d, bpb + 17)
    fat_sectors = le16(d, bpb + 22)
    if le16(d, bpb + 11) != SECTOR or spc != 1 or nfats != 2 or root_entries != 512 or fat_sectors != 128:
        raise ValueError("unexpected FAT16 BPB; refusing to alter")
    root_sectors = (root_entries * 32 + SECTOR - 1) // SECTOR
    data_sector = pstart + reserved + nfats * fat_sectors + root_sectors
    cluster_count = (psectors - reserved - nfats * fat_sectors - root_sectors) // spc
    if not (4085 <= cluster_count < 65525):
        raise ValueError("not a FAT16 volume")
    root_off = (pstart + reserved + nfats * fat_sectors) * SECTOR
    if any(d[root_off:root_off + root_entries * 32]):
        raise ValueError("FAT16 root is not empty; refusing to overwrite files")

    def cluster_offset(cluster: int) -> int:
        if cluster < 2 or cluster >= cluster_count + 2:
            raise ValueError("cluster out of range")
        return (data_sector + (cluster - 2) * spc) * SECTOR

    data_clusters = (len(efi_binary) + SECTOR * spc - 1) // (SECTOR * spc)
    if 3 + data_clusters > cluster_count:
        raise ValueError("EFI file does not fit in FAT16 partition")
    # Cluster 2 = EFI/, 3 = BOOT/, 4.. = EFI binary.
    chain = {2: 0xFFFF, 3: 0xFFFF}
    for n in range(data_clusters):
        c = 4 + n
        chain[c] = c + 1 if n < data_clusters - 1 else 0xFFFF
    for i in range(nfats):
        fat_off = (pstart + reserved + i * fat_sectors) * SECTOR
        if d[fat_off:fat_off + 4] != b"\xf8\xff\xff\xff":
            raise ValueError("FAT does not have expected empty-volume header")
        for c, next_c in chain.items():
            struct.pack_into("<H", d, fat_off + 2 * c, next_c)
    d[root_off:root_off + 32] = dir_entry(b"EFI        ", 0x10, 2)
    efi_dir = cluster_offset(2)
    d[efi_dir:efi_dir + 32] = dir_entry(b".          ", 0x10, 2)
    d[efi_dir + 32:efi_dir + 64] = dir_entry(b"..         ", 0x10, 0)
    d[efi_dir + 64:efi_dir + 96] = dir_entry(b"BOOT       ", 0x10, 3)
    boot_dir = cluster_offset(3)
    d[boot_dir:boot_dir + 32] = dir_entry(b".          ", 0x10, 3)
    d[boot_dir + 32:boot_dir + 64] = dir_entry(b"..         ", 0x10, 2)
    d[boot_dir + 64:boot_dir + 96] = dir_entry(b"BOOTX64 EFI", 0x20, 4, len(efi_binary))
    for n in range(data_clusters):
        off = cluster_offset(4 + n)
        chunk = efi_binary[n * SECTOR:(n + 1) * SECTOR]
        d[off:off + len(chunk)] = chunk
    # The separate UEFI test image is marked with the standard MBR ESP type.
    # No other byte of MBR, Stage1, kernel or VBR changes.
    d[part + 4] = 0xEF
    expected_pre = bytearray(bios_image[:pstart * SECTOR])
    expected_pre[part + 4] = 0xEF
    if d[:pstart * SECTOR] != expected_pre or d[bpb:bpb + SECTOR] != bios_image[bpb:bpb + SECTOR]:
        raise AssertionError("BIOS code or FAT VBR was modified")
    return bytes(d)


def extract(d: bytes) -> bytes:
    """Read our specific staged file back via FAT16 directory entries and FAT chain."""
    pstart = le32(d, 454)
    bpb = pstart * SECTOR
    reserved = le16(d, bpb + 14)
    nfats = d[bpb + 16]
    fat_sectors = le16(d, bpb + 22)
    root_entries = le16(d, bpb + 17)
    root_off = (pstart + reserved + nfats * fat_sectors) * SECTOR
    data_off = (pstart + reserved + nfats * fat_sectors + (root_entries * 32 + 511) // 512) * SECTOR
    fat_off = (pstart + reserved) * SECTOR

    def entry(off: int, expected_name: bytes) -> tuple[int, int]:
        if d[off:off + 11] != expected_name:
            raise ValueError("missing FAT16 entry: " + repr(expected_name))
        return le16(d, off + 26), le32(d, off + 28)

    efi_c, _ = entry(root_off, b"EFI        ")
    boot_c, _ = entry(data_off + (efi_c - 2) * 512 + 64, b"BOOT       ")
    file_c, file_size = entry(data_off + (boot_c - 2) * 512 + 64, b"BOOTX64 EFI")
    output = bytearray()
    seen = set()
    c = file_c
    while c < 0xFFF8:
        if c in seen or c < 2:
            raise ValueError("FAT16 cycle or illegal cluster")
        seen.add(c)
        off = data_off + (c - 2) * 512
        output.extend(d[off:off + 512])
        c = le16(d, fat_off + 2 * c)
    return bytes(output[:file_size])


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: add_uefi_fat16.py BIOS.img BOOTX64.EFI OUTPUT.img")
    bios = pathlib.Path(sys.argv[1]).read_bytes()
    efi = pathlib.Path(sys.argv[2]).read_bytes()
    out = pathlib.Path(sys.argv[3])
    result = construct(bios, efi)
    if extract(result) != efi:
        raise SystemExit("FAT16 roundtrip failed")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(result)
    print(f"UEFI FAT16 image: {out}, {len(result)//1048576} MiB, EFI file {len(efi)} bytes")


if __name__ == "__main__":
    main()
