"""C1: compile and exercise console parser in userspace, no QEMU needed."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TestConsole64(unittest.TestCase):
    def test_monitor_parser_host(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'console64-host'
            subprocess.run([
                'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-DINTELLECT_HW64_HOST_TEST', '-DINTELLECT_BLOCK64_HOST_TEST', '-Ikernel/include', 'kernel/src64/kmonitor64.c', 'kernel/src64/hardware64.c', 'kernel/src64/driver64.c', 'kernel/src64/hwdiag64.c', 'kernel/src64/ata_pio64.c',
                    'kernel/src64/block64.c', 'kernel/src64/partition64.c',
                    'kernel/src64/diskdiag64.c',
                'tests/console64_host.c', '-o', str(output)
            ], cwd=ROOT, capture_output=True, text=True, check=True)
            p = subprocess.run([str(output)], cwd=ROOT, capture_output=True,
                               text=True, check=True)
            self.assertIn('PASS: C1', p.stdout)

    def test_new_build_path_and_invocation(self):
        make = (ROOT / 'Makefile').read_text()
        kernel = (ROOT / 'kernel/src64/kernel.c').read_text()
        self.assertIn('CONSOLE64_OBJS', make)
        self.assertIn('uefi-console:', make)
        self.assertIn('intellect_monitor64_run(boot)', kernel)
        self.assertIn('intellect_serial64_init()', kernel)
        self.assertIn('intellect_serial64_putc(ch)', kernel)
        self.assertIn('intellect_vmm64_self_test()', kernel)
        self.assertIn('intellect64_memory_core_start(', kernel)


if __name__ == '__main__':
    unittest.main()
