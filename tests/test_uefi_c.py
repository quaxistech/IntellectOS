"""Stage C platform discovery regression tests with deterministic UEFI mocks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class UefiStageCTests(unittest.TestCase):
    def test_platform_probe_synthetic_firmware(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/"platform-host"
            compile_cmd=[
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fshort-wchar",
                "-Iboot/uefi", "boot/uefi/platform_probe.c", "boot/uefi/firmware_info.c",
                "tests/uefi_platform_host.c", "-o", str(binary)
            ]
            subprocess.run(compile_cmd,cwd=ROOT,check=True,capture_output=True,text=True)
            result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
            self.assertIn("PASS: spec-indexed UEFI BootServices",result.stdout)

    def test_efi_boot_service_abi_offsets(self):
        header=(ROOT/"boot/uefi/efi_min.h").read_text()
        self.assertIn("get_memory_map) == 56",header)
        self.assertIn("stall) == 248",header)
        self.assertIn("locate_protocol) == 320",header)
        source=(ROOT/"boot/uefi/platform_probe.c").read_text()
        self.assertIn("used_size%descriptor_size",source)
        self.assertNotIn("ExitBootServices(",source)

if __name__=="__main__": unittest.main()
