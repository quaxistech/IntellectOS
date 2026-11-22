"""Tests for target disk installer (wipe, partition, FAT32 ESP, IntellectFS v1).

Never touches host disks; runs exclusively in temporary files and mocks.
"""
from pathlib import Path
import tempfile
import unittest
import uuid
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from installer.target_disk_installer import (
    validate_target_safety,
    wipe_target_disk,
    partition_target_disk,
    format_esp,
    format_intellectfs,
    install_target_disk,
    TargetSafetyError,
    InstallationError,
    ESP_TYPE,
    IFS_GPT_GUID
)
from tools.verify_installed_system import verify


class TargetDiskInstallerTests(unittest.TestCase):
    def test_refuses_host_system_disks(self):
        for host_disk in ('/dev/sda', '/dev/nvme0n1', '/dev/vda', '/dev/sda1'):
            with self.subTest(disk=host_disk):
                with self.assertRaises(TargetSafetyError):
                    validate_target_safety(host_disk, allow_real_usb=False)
                with self.assertRaises(TargetSafetyError):
                    validate_target_safety(host_disk, allow_real_usb=True,
                                           confirm_token=f"INSTALL {host_disk}")

    def test_wipe_and_partition_image(self):
        with tempfile.TemporaryDirectory() as td:
            img = Path(td) / 'test_wipe.img'
            # Pre-fill with dirty bytes
            img.write_bytes(b'DIRTY_GARBAGE_BYTES' * 1000)
            size = wipe_target_disk(img, size_bytes=1024 * 1024 * 1024)
            self.assertEqual(size, 1024 * 1024 * 1024)

            # Check that first 1 MiB is zeroed
            with img.open('rb') as f:
                first_1mb = f.read(1024 * 1024)
                self.assertEqual(first_1mb, bytes(1024 * 1024))

            # Partition
            part_info = partition_target_disk(img, size)
            self.assertEqual(part_info['esp_start_lba'], 2048)
            self.assertEqual(part_info['ifs_start_lba'], 1050624)
            self.assertGreater(part_info['ifs_blocks'], 65536)

            # Check MBR signature
            with img.open('rb') as f:
                mbr = f.read(512)
                self.assertEqual(mbr[510:512], b'\x55\xaa')
                self.assertEqual(mbr[446 + 4], 0xee)  # Protective type

    def test_full_installation_and_verification(self):
        with tempfile.TemporaryDirectory() as td:
            img = Path(td) / 'test_install.img'
            efi = (ROOT / 'build/uefi/BOOTX64.EFI').read_bytes()
            ivx = (ROOT / 'build/user/terminal.ivx').read_bytes()
            pub = (ROOT / 'build/user/terminal.pub').read_bytes()

            receipt = install_target_disk(
                target=img,
                efi_bytes=efi,
                ivx_bytes=ivx,
                pub_bytes=pub,
                size_gib=1,
                wipe=True,
                timezone='UTC'
            )
            self.assertEqual(receipt['status'], 'SUCCESS_INSTALLED')
            self.assertTrue(img.is_file())

            # Independent verification
            v = verify(img, efi=efi)
            self.assertEqual(v['timezone'], 'UTC')
            self.assertTrue(v['ivx_signed'])
            self.assertIn('/sbin/terminal.ivx', v['system_files'])
            self.assertIn('/etc/system.json', v['system_files'])
            self.assertIn('/etc/timezone', v['system_files'])


if __name__ == '__main__':
    unittest.main()
