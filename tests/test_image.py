"""Host-side tests that never touch physical storage or hardware."""
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from mkimage import construct


def mbr_bytes():
    data = bytearray(512)
    data[510:512] = b"\x55\xaa"
    data[446] = 0x80
    struct.pack_into("<II", data, 454, 2048, 32768)
    struct.pack_into("<H", data, 14, 17)
    return bytes(data)


class ImageTests(unittest.TestCase):
    def test_rejects_oversized_kernel(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "kernel too large"):
                construct(mbr_bytes(), b"a", b"x" * 65537,
                          pathlib.Path(tmp) / "image.img")

    def test_rejects_oversized_stage1(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "stage1 too large"):
                construct(mbr_bytes(), b"z" * 8193, b"kernel",
                          pathlib.Path(tmp) / "image.img")

    def test_rejects_empty_kernel(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "empty kernel"):
                construct(mbr_bytes(), b"stage", b"",
                          pathlib.Path(tmp) / "image.img")

    def test_rejects_wrong_partition_start(self):
        mbr = bytearray(mbr_bytes())
        struct.pack_into("<I", mbr, 454, 4096)
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "partition layout"):
                construct(bytes(mbr), b"stage", b"kernel",
                          pathlib.Path(tmp) / "image.img")

    def test_build_preserves_loader_and_kernel_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = pathlib.Path(tmp) / "image.img"
            stage = b"unique-stage-marker-123"
            kernel = b"unique-kernel-marker-456"
            construct(mbr_bytes(), stage, kernel, image)
            with image.open("rb") as f:
                f.seek(512)
                self.assertEqual(f.read(len(stage)), stage)
                f.seek(17 * 512)
                self.assertEqual(f.read(len(kernel)), kernel)
                f.seek(2048 * 512 + 510)
                self.assertEqual(f.read(2), b"\x55\xaa")

    def test_rejects_bad_signature(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "MBR"):
                construct(bytes(512), b"stage", b"kernel",
                          pathlib.Path(tmp) / "image.img")


if __name__ == "__main__":
    unittest.main()
