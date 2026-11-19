"""Memory Core M1 tests; independent of QEMU/Lenovo runtime evidence."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parent.parent

class TestVMM64(unittest.TestCase):
    def test_high_half_scratch_vmm_on_host(self):
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'vmm64-host'
            args=['gcc','-std=c11','-Wall','-Wextra','-Werror','-O2',
                  '-DINTELLECT_VMM64_HOST_TEST','-Ikernel/include',
                  'kernel/src64/pmm64.c','kernel/src64/vmm64.c',
                  'tests/vmm64_host.c','-o',str(binary)]
            subprocess.run(args,cwd=ROOT,capture_output=True,text=True,check=True)
            result=subprocess.run([str(binary)],cwd=ROOT,capture_output=True,
                                  text=True,check=True)
            self.assertIn('PASS: VMM64',result.stdout)

    def test_kernel_entry_passes_four_arguments(self):
        entry=(ROOT/'kernel/x86_64/entry.S').read_text()
        self.assertIn('mov %rsp, %rdx',entry)
        self.assertIn('mov %rax, %rcx',entry)
        self.assertIn('call intellect64_main',entry)

    def test_vmm_in_live_kernel_and_build(self):
        c=(ROOT/'kernel/src64/kernel.c').read_text()
        make=(ROOT/'Makefile').read_text()
        self.assertIn('intellect_vmm64_init(bootstrap_cr3)',c)
        self.assertIn('intellect_vmm64_self_test()',c)
        self.assertIn('vmm64',make)
        self.assertIn('MEMCORE64_OBJS',make)

if __name__=='__main__': unittest.main()
