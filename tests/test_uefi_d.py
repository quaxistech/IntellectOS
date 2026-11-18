"""UEFI Stage D tests: independently indexed Boot Services ABI and BootInfo semantics."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class UefiStageDTests(unittest.TestCase):
    def test_spec_indexed_uefi_builder_fixture(self):
        with tempfile.TemporaryDirectory() as td:
            target = Path(td) / "uefi-bootinfo-host"
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fshort-wchar", "-Iboot/uefi",
                            "boot/uefi/bootinfo_builder.c", "boot/uefi/firmware_info.c",
                            "tests/uefi_bootinfo_host.c", "-o", str(target)],
                           cwd=ROOT, check=True, capture_output=True, text=True)
            result = subprocess.run([str(target)], check=True, capture_output=True, text=True)
            self.assertIn("PASS: Stage D independent UEFI ABI", result.stdout)

    def test_bootinfo_abi_v1_and_firmware_snapshot_contract(self):
        s = (ROOT / "boot/uefi/bootinfo_builder.c").read_text()
        h = (ROOT / "boot/uefi/efi_min.h").read_text()
        self.assertIn("allocate_pages) == 40", h)
        self.assertIn("locate_protocol) == 320", h)
        self.assertIn("INTELLECT_BOOT_UEFI", s)
        self.assertIn("INTELLECT_MEMORY_ACPI_NVS", s)
        self.assertIn("INTELLECT_FLAG_MEMORY_MAP_VALID", s)
        self.assertNotIn("ExitBootServices(", s)

if __name__ == "__main__":
    unittest.main()
