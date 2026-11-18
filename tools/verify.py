#!/usr/bin/env python3
"""Read-only structural checks of built images and i386 kernel."""
from __future__ import annotations
import pathlib
import struct
import sys


def verify(img: pathlib.Path, stage: bytes, kernel: bytes, elf: bytes):
    if img.stat().st_size != 32 * 1024 * 1024:
        raise AssertionError("image size")
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise AssertionError("kernel is not ELF32 little-endian")
    if struct.unpack_from("<H", elf, 18)[0] != 3:
        raise AssertionError("kernel e_machine is not i386")
    if struct.unpack_from("<I", elf, 24)[0] != 0x10000:
        raise AssertionError("kernel entry is not physical 0x10000")
    with img.open("rb") as f:
        def sector(lba, count=1):
            f.seek(lba * 512)
            return f.read(count * 512)
        mbr = sector(0)
        if mbr[510:] != b"\x55\xaa":
            raise AssertionError("missing BIOS 55AA signature")
        if mbr[446] != 0x80 or mbr[450] != 6:
            raise AssertionError("invalid active FAT16 partition")
        if struct.unpack_from("<II", mbr, 454) != (2048, 32768):
            raise AssertionError("unexpected partition")
        if sector(1, 16)[:len(stage)] != stage:
            raise AssertionError("stage1 image mismatch")
        if sector(17, 128)[:len(kernel)] != kernel:
            raise AssertionError("kernel image mismatch")
        if sector(2048) != mbr:
            raise AssertionError("VBR mismatch")
        if struct.unpack_from("<H", mbr, 11)[0] != 512:
            raise AssertionError("FAT16 sector size mismatch")
        if mbr[13] != 1 or struct.unpack_from("<H", mbr, 14)[0] != 17:
            raise AssertionError("FAT16 reserved layout mismatch")
        if sector(1, 16)[len(stage):] != bytes(16 * 512 - len(stage)):
            raise AssertionError("stage1 padding is nonzero")
        if sector(17, 128)[len(kernel):] != bytes(128 * 512 - len(kernel)):
            raise AssertionError("kernel padding is nonzero")
        if sector(2049, 16)[:len(stage)] != stage:
            raise AssertionError("stage1 mirror mismatch")
        if sector(2065)[:4] != b"\xf8\xff\xff\xff":
            raise AssertionError("FAT16 signature missing")
    if len(stage) > 8192 or len(kernel) > 65536:
        raise AssertionError("fixed allocation exceeded")
    print("PASS: BIOS signature + active MBR FAT16 + VBR + stage1 + kernel image")
    print("PASS: valid i386 ELF entry @ 0x10000; size bounds")
    print("NOTE: boot execution and firmware table discovery require runtime testing")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit("usage: verify.py IMAGE STAGE1 KERNEL ELF")
    verify(pathlib.Path(sys.argv[1]), *(pathlib.Path(x).read_bytes() for x in sys.argv[2:]))
