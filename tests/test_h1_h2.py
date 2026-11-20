"""H1/H2 deterministic host and linkage tests (no physical PCI I/O)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class H1H2Tests(unittest.TestCase):
    def test_host_hardware_and_driver_management(self):
        with tempfile.TemporaryDirectory() as temp:
            for opt in ('-O0','-O2'):
                binary=Path(temp)/('h1_h2_'+opt)
                subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',opt,
                    '-DINTELLECT_HW64_HOST_TEST','-Ikernel/include',
                    'kernel/src64/hardware64.c','kernel/src64/driver64.c',
                    'kernel/src64/hwdiag64.c','tests/hardware64_host.c',
                    '-o',str(binary)],cwd=ROOT,check=True,capture_output=True,text=True)
                r=subprocess.run([str(binary)],cwd=ROOT,check=True,capture_output=True,text=True)
                self.assertIn('PASS: H1/H2',r.stdout)
    def test_build_hooks(self):
        make=(ROOT/'Makefile').read_text()
        k=(ROOT/'kernel/src64/kernel.c').read_text()
        mon=(ROOT/'kernel/src64/kmonitor64.c').read_text()
        self.assertIn('$(H1H2_OBJS)',make)
        self.assertIn('intellect_hw64_init(boot)',k)
        self.assertIn('intellect_driver64_init(intellect_hw64_snapshot())',k)
        self.assertIn('intellect_hwdiag64_command(',mon)
        self.assertIn('uefi-hw-smoke:',make)
        self.assertIn('M3-A IDENTITY W^X/NX: PASS',k)

if __name__=='__main__': unittest.main()
