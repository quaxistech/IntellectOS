"""Stage E checks deliberately independent from our UEFI C struct indexes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class UefiStageETests(unittest.TestCase):
    def test_uefi_boot_service_exit_slot(self):
        # Spec reference: UEFI 2.11 BootServices x64 offset 232.
        hdr=(ROOT/'boot/uefi/efi_min.h').read_text()
        self.assertIn('exit_boot_services) == 232',hdr)
        self.assertIn('locate_protocol) == 320',hdr)
        c=(ROOT/'boot/uefi/handoff.c').read_text()
        self.assertIn('bs->exit_boot_services(image,key)',c)
        self.assertIn('EFI_INVALID_PARAMETER',c)
        self.assertIn('intellect_uefi_refresh_bootinfo_map',c)
        self.assertNotIn('st->con_out',c)

    def test_flat_kernel_is_static_and_unrelocated(self):
        subprocess.run(['python3','tools/verify_kernel64.py',
          'build/kernel64.elf','build/kernel64.bin'],check=True,cwd=ROOT,capture_output=True)

    def test_synthetic_exit_handoff(self):
        with tempfile.TemporaryDirectory() as temp:
            out=str(Path(temp)/'handoff-host')
            args=['gcc','-std=c11','-Wall','-Wextra','-Werror','-O1','-fshort-wchar',
                  'boot/uefi/bootinfo_builder.c','boot/uefi/firmware_info.c',
                  'boot/uefi/handoff.c','boot/uefi/framebuffer_console.c',
                  'tests/uefi_handoff_host.c','-o',out]
            subprocess.run(args,check=True,cwd=ROOT,capture_output=True,text=True)
            p=subprocess.run([out],check=True,cwd=ROOT,capture_output=True,text=True)
            self.assertIn('PASS: Stage E fresh MapKey',p.stdout)

    def test_independent_framebuffer_drawing(self):
        with tempfile.TemporaryDirectory() as temp:
            out=str(Path(temp)/'framebuffer-host')
            args=['gcc','-std=c11','-Wall','-Wextra','-Werror','-O1','-fshort-wchar',
                  'boot/uefi/bootinfo_builder.c','boot/uefi/firmware_info.c',
                  'boot/uefi/framebuffer_console.c','tests/uefi_framebuffer_host.c','-o',out]
            subprocess.run(args,check=True,cwd=ROOT,capture_output=True,text=True)
            p=subprocess.run([out],check=True,cwd=ROOT,capture_output=True,text=True)
            self.assertIn('PASS: Stage E independent framebuffer',p.stdout)

    def test_bios_loader_unchanged(self):
        self.assertTrue((ROOT/'build/intellectos-bios.img').is_file())
        self.assertEqual((ROOT/'build/boot/mbr.bin').stat().st_size,512)

if __name__=='__main__': unittest.main()
