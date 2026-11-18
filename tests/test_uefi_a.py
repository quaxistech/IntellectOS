"""Host-only FAT16 UEFI image invariants; tests do not access disks."""
from __future__ import annotations

import pathlib
import struct
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from add_uefi_fat16 import construct, extract  # noqa: E402


class UefiStageATests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        raw = bytearray(32 * 1024 * 1024)
        raw[510:512] = b"\x55\xaa"
        raw[446] = 0x80
        raw[450] = 0x06
        struct.pack_into("<II", raw, 454, 2048, 32768)
        bpb = 2048 * 512
        raw[bpb + 510:bpb + 512] = b"\x55\xaa"
        struct.pack_into("<H", raw, bpb + 11, 512)
        raw[bpb + 13] = 1
        struct.pack_into("<H", raw, bpb + 14, 17)
        raw[bpb + 16] = 2
        struct.pack_into("<H", raw, bpb + 17, 512)
        struct.pack_into("<H", raw, bpb + 22, 128)
        for n in range(2):
            start = (2048 + 17 + n * 128) * 512
            raw[start:start+4] = b"\xf8\xff\xff\xff"
        cls.bios = bytes(raw)

    def test_efi_file_roundtrip_and_bios_immutable(self):
        contents = b"MZ" + bytes((i % 251 for i in range(4000)))
        combined = construct(self.bios, contents)
        self.assertEqual(combined[450], 0xEF)
        self.assertEqual(combined[:450], self.bios[:450])
        self.assertEqual(combined[451:2048*512], self.bios[451:2048*512])
        self.assertEqual(combined[2048*512:2049*512], self.bios[2048*512:2049*512])
        self.assertEqual(extract(combined), contents)

    def test_reject_existing_files(self):
        original = bytearray(self.bios)
        original[(2048 + 17 + 2*128) * 512] = 0x41
        with self.assertRaisesRegex(ValueError, "not empty"):
            construct(bytes(original), b"MZexample")

    def test_reject_oversized_efi(self):
        with self.assertRaisesRegex(ValueError, "1 MiB"):
            construct(self.bios, b"MZ" + b"x" * (1024*1024))


if __name__ == "__main__":
    unittest.main()
