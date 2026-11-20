"""M2 kernel-heap host tests; not a substitute for QEMU/physical boot."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parent.parent


class Kheap64Tests(unittest.TestCase):
    def test_bounded_heap_host(self):
        with tempfile.TemporaryDirectory() as directory:
            for optimization in ('-O0','-O2'):
                binary=Path(directory)/('kheap-'+optimization[1:])
                command=['gcc','-std=c11',optimization,'-Wall','-Wextra','-Werror',
                         '-DINTELLECT_VMM64_HOST_TEST',
                         '-DINTELLECT_KHEAP64_HOST_TEST','-Ikernel/include',
                         'kernel/src64/pmm64.c','kernel/src64/vmm64.c',
                         'kernel/src64/kheap64.c','tests/kheap64_host.c',
                         '-o',str(binary)]
                subprocess.run(command,cwd=ROOT,check=True,capture_output=True,text=True)
                result=subprocess.run([str(binary)],cwd=ROOT,check=True,
                                      capture_output=True,text=True)
                self.assertIn('PASS: KHEAP64',result.stdout)

    def test_live_kernel_and_build_wiring(self):
        kernel=(ROOT/'kernel/src64/kernel.c').read_text()
        make=(ROOT/'Makefile').read_text()
        self.assertIn('intellect_kheap64_init()',kernel)
        self.assertIn('intellect_kheap64_self_test()',kernel)
        self.assertIn('kheap64',make)
        self.assertIn('KHEAP SLAB/PAGE-RUN: PASS',kernel)


if __name__=='__main__':
    unittest.main()
