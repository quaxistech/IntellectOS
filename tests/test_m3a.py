"""M3-A pure-policy tests; hardware page-table transition needs QEMU evidence."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class M3APolicyTests(unittest.TestCase):
    def test_host_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            for opt in ("-O0", "-O2"):
                binary = Path(tmp) / ("m3policy" + opt)
                cmd = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", opt,
                       "-Ikernel/include", "kernel/src64/m3policy64.c",
                       "tests/m3policy64_host.c", "-o", str(binary)]
                subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True)
                out = subprocess.run([str(binary)], cwd=ROOT,
                                     capture_output=True, text=True, check=True)
                self.assertIn("PASS: M3-A", out.stdout)

    def test_memory_protection_hook(self):
        kernel = (ROOT / "kernel/src64/kernel.c").read_text()
        make = (ROOT / "Makefile").read_text()
        ld = (ROOT / "kernel/x86_64/linker.ld").read_text()
        for required in ("intellect_m3_protect64_init(bootstrap_cr3,kernel_base,stack_top)",
                         "intellect_m3_protect64_self_test()"):
            self.assertIn(required,kernel)
        self.assertIn("$(M3A_OBJS)",make)
        self.assertIn("__intellect_text_end",ld)
        self.assertIn("__intellect_bss_end",ld)

    def test_runtime_binary_checks(self):
        binary = ROOT / "build/kernel64.elf"
        if not binary.exists():
            self.skipTest("run make check first")
        out = subprocess.run(["readelf", "-W", "-s", str(binary)], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout
        self.assertIn("__intellect_text_end",out)
        self.assertIn("__intellect_rodata_end",out)
        self.assertIn("__intellect_bss_end",out)

if __name__ == '__main__': unittest.main()
