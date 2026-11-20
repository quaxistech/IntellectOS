"""B1 early menu host checks plus integration anchors on the actual tree."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class BootMenuTests(unittest.TestCase):
    def test_menu_and_graphical_gate(self):
        for opt in ('-O0','-O2'):
            with self.subTest(opt=opt), tempfile.TemporaryDirectory() as d:
                binary=Path(d)/'bootmenu-test'
                subprocess.run(['cc','-std=c11','-fshort-wchar','-Wall','-Wextra',
                                '-Werror',opt,'-Iboot/uefi','-Ikernel/include',
                                'boot/uefi/bootmenu.c','tests/uefi_bootmenu_host.c',
                                '-o',str(binary)],cwd=ROOT,capture_output=True,text=True,check=True)
                process=subprocess.run([str(binary)],capture_output=True,text=True,check=True)
                self.assertIn('PASS: boot menu',process.stdout)
    def test_firmware_block_inventory(self):
        with tempfile.TemporaryDirectory() as d:
            binary=Path(d)/'uefi-block-test'
            subprocess.run(['cc','-std=c11','-fshort-wchar','-Wall','-Wextra',
                            '-Werror','-O2','-Iboot/uefi',
                            'boot/uefi/firmware_storage.c',
                            'tests/uefi_firmware_storage_host.c',
                            '-o',str(binary)],cwd=ROOT,capture_output=True,text=True,check=True)
            proc=subprocess.run([str(binary)],capture_output=True,text=True,check=True)
            self.assertIn('PASS: UEFI Block I/O',proc.stdout)
    def test_quiet_boot_integration(self):
        start=(ROOT/'boot/uefi/start.c').read_text()
        kernel=(ROOT/'kernel/src64/kernel.c').read_text()
        mk=(ROOT/'Makefile').read_text()
        self.assertIn('intellect_uefi_bootmenu',start)
        self.assertNotIn('uefi_probe_platform(st)',start)
        self.assertNotIn('uefi_print_firmware_info(st)',start)
        self.assertNotIn('intellect_uefi_draw_pre_exit(&snapshot)',start)
        self.assertIn('visual_diagnostics_enabled',kernel)
        self.assertIn('$(BOOTMENU_EFI_OBJS)',mk)
    def test_uefi_firmware_block_discovery_freestanding(self):
        with tempfile.TemporaryDirectory() as d:
            for source in ('bootmenu','firmware_storage'):
                subprocess.run(['clang','--target=x86_64-pc-win32-coff','-std=c11',
                                '-fshort-wchar','-ffreestanding','-fno-stack-protector',
                                '-fno-builtin','-mno-red-zone','-O2','-Wall','-Wextra',
                                '-Werror','-c',f'boot/uefi/{source}.c',
                                '-o',str(Path(d)/(source+'.obj'))],cwd=ROOT,
                                capture_output=True,text=True,check=True)
if __name__=='__main__':unittest.main()
