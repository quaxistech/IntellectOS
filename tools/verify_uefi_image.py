#!/usr/bin/env python3
"""Check PE32+ UEFI x64 executable and unchanged legacy BIOS instructions in UEFI test image."""
import pathlib
import struct
import sys
from add_uefi_fat16 import extract


def verify_pe(data: bytes) -> None:
    if len(data) < 256 or data[:2] != b"MZ":
        raise ValueError("missing MZ header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 24 + 70 >= len(data) or data[pe:pe+4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    machine = struct.unpack_from("<H", data, pe+4)[0]
    optional = pe+24
    magic = struct.unpack_from("<H", data, optional)[0]
    subsystem = struct.unpack_from("<H", data, optional+68)[0]
    relocation_rva, relocation_size = struct.unpack_from("<II", data, optional+112+5*8)
    if machine != 0x8664 or magic != 0x20B or subsystem != 10 or not relocation_rva or not relocation_size:
        raise ValueError(f"not relocatable x64 EFI application (machine={machine:#x}, magic={magic:#x}, subsystem={subsystem})")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: verify_uefi_image.py BIOS.img BOOTX64.EFI DUAL.img")
    bios, efi, dual = (pathlib.Path(p).read_bytes() for p in sys.argv[1:])
    verify_pe(efi)
    expected_pre = bytearray(bios[:2048*512])
    expected_pre[450] = 0xEF
    if len(bios) != len(dual) or dual[:2048*512] != expected_pre:
        raise SystemExit("FAIL: BIOS code/Stage1/kernel changed")
    if dual[450] != 0xEF:
        raise SystemExit("FAIL: UEFI ESP partition type missing")
    if bios[2048*512:2049*512] != dual[2048*512:2049*512]:
        raise SystemExit("FAIL: FAT VBR changed")
    if extract(dual) != efi:
        raise SystemExit("FAIL: BOOTX64.EFI mismatch in FAT16 image")
    print("PASS: relocatable native x64 PE32+ EFI application; subsystem=EFI_APPLICATION")
    print("PASS: EFI System MBR partition type=0xEF; BIOS code/Stage1/kernel/FAT VBR unchanged")
    print("PASS: EFI/BOOT/BOOTX64.EFI read back unchanged from FAT16")
    print("NOTE: runtime UEFI boot is not proven by these checks")

if __name__ == "__main__":
    main()
