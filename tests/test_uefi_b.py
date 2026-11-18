"""Host-side executable tests of Stage B's firmware field formatting."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class UefiStageBTests(unittest.TestCase):
    def test_firmware_formatting_with_synthetic_system_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable = pathlib.Path(tmp) / "uefi-firmware-test"
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fshort-wchar",
                "-Iboot/uefi", "boot/uefi/firmware_info.c",
                "tests/uefi_firmware_host.c", "-o", str(executable),
            ], cwd=ROOT, check=True, capture_output=True, text=True)
            run = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
            self.assertIn("PASS: host UEFI firmware formatting", run.stdout)

    def test_struct_layout_and_uefi_contract_are_explicit(self):
        header = (ROOT / "boot/uefi/efi_min.h").read_text()
        source = (ROOT / "boot/uefi/start.c").read_text()
        self.assertIn("offsetof(efi_system_table_t, boot_services) == 96", header)
        self.assertIn("st->hdr.header_size < sizeof(efi_system_table_t)", source)
        self.assertIn("intellect_uefi_handoff", source)  # Stage E advances beyond the Stage B firmware probe


if __name__ == "__main__":
    unittest.main()
