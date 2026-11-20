"""S0 tests: controlled host ATA emulation and build wiring."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class S0StorageTests(unittest.TestCase):
    def test_readonly_ata_mbr_gpt_host(self):
        with tempfile.TemporaryDirectory() as t:
            for opt in ('-O0','-O2'):
                binary=Path(t)/('block'+opt)
                cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror',opt,
                     '-DINTELLECT_BLOCK64_HOST_TEST','-Ikernel/include',
                     'kernel/src64/ata_pio64.c','kernel/src64/block64.c',
                     'kernel/src64/partition64.c','kernel/src64/diskdiag64.c',
                     'kernel/src64/driver64.c',
                     'tests/block64_host.c','-o',str(binary)]
                subprocess.run(cmd,cwd=ROOT,check=True,capture_output=True,text=True)
                result=subprocess.run([str(binary)],cwd=ROOT,check=True,
                                      capture_output=True,text=True)
                self.assertIn('PASS: S0 ATA PIO',result.stdout)
    def test_kernel_smoke_wiring(self):
        m=(ROOT/'Makefile').read_text()
        k=(ROOT/'kernel/src64/kernel.c').read_text()
        monitor=(ROOT/'kernel/src64/kmonitor64.c').read_text()
        for s in ('$(S0_OBJS)','uefi-s0-smoke:'):
            self.assertIn(s,m)
        self.assertIn('intellect_block64_init(intellect_hw64_snapshot())',k)
        self.assertIn('intellect_diskdiag64_command(',monitor)
        self.assertIn('0xECu', (ROOT/'kernel/src64/ata_pio64.c').read_text())

if __name__=='__main__': unittest.main()
