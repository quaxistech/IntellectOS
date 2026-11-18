#!/usr/bin/env python3
"""Create a 32 MiB raw Legacy-BIOS USB-HDD image. No physical disk writes.

LBA 0:         custom MBR / boot sector
LBA 1..16:     8 KiB BIOS stage1
LBA 17..144:   64 KiB protected-mode kernel (fixed boot allocation)
LBA 2048:      FAT16 VBR boot sector (dual-purpose boot sector for firmware quirks)
LBA 2049..64: mirrored BIOS stage1 for diagnostic compatibility
FAT16 volume:  empty, beginning LBA 2048, valid basic BPB

The stage1 always loads the kernel from *absolute* LBA17. This is a bootstrap
format, NOT a general-purpose filesystem loader. FAT16 is not used for code.
"""
from __future__ import annotations

import pathlib
import struct
import sys

SECTOR = 512
TOTAL = 65536
PART_START = 2048
PART_SECTORS = 32768
STAGE_SECTORS = 16
KERNEL_SECTORS = 128
FAT_SECTORS = 128
FAT_START = PART_START + 17


def construct(mbr: bytes, stage1: bytes, kernel: bytes, output: pathlib.Path):
    if len(mbr) != SECTOR or mbr[510:512] != b"\x55\xaa":
        raise ValueError("invalid 512-byte MBR")
    if len(stage1) > STAGE_SECTORS * SECTOR:
        raise ValueError(f"stage1 too large: {len(stage1)} bytes")
    if len(kernel) > KERNEL_SECTORS * SECTOR:
        raise ValueError(f"kernel too large: {len(kernel)} bytes")
    if len(kernel) == 0:
        raise ValueError("empty kernel")
    if struct.unpack_from("<II", mbr, 454) != (PART_START, PART_SECTORS):
        raise ValueError("MBR partition layout mismatch")
    if struct.unpack_from("<H", mbr, 14)[0] != 17:
        raise ValueError("FAT16 reserved sector count mismatch")
    if 17 + KERNEL_SECTORS >= PART_START:
        raise ValueError("kernel overlaps partition")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as f:
        f.truncate(TOTAL * SECTOR)
        for lba, content, capacity in (
            (0, mbr, 1),
            (1, stage1, STAGE_SECTORS),
            (17, kernel, KERNEL_SECTORS),
            (PART_START, mbr, 1),
            (PART_START + 1, stage1, STAGE_SECTORS),
        ):
            f.seek(lba * SECTOR)
            f.write(content.ljust(capacity * SECTOR, b"\x00"))
        for n in range(2):
            f.seek((FAT_START + n * FAT_SECTORS) * SECTOR)
            f.write(b"\xf8\xff\xff\xff")
    print(f"Image created: {output}, {output.stat().st_size // (1024*1024)} MiB")
    print(f"Stage1={len(stage1)} B / 8192 B, kernel={len(kernel)} B / 65536 B")


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: mkimage.py MBR.bin STAGE1.bin KERNEL.bin IMAGE.img")
    try:
        construct(*(pathlib.Path(x).read_bytes() for x in sys.argv[1:4]),
                  pathlib.Path(sys.argv[4]))
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc


if __name__ == "__main__":
    main()
