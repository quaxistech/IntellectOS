"""Run by `make check`: regression for the absolute GDT/IDT pointer failure."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parent.parent


class DescriptorRuntimeRelocationTests(unittest.TestCase):
    def test_descriptor_pointer_built_at_runtime(self):
        text = (ROOT / 'kernel/src64/exceptions64.c').read_text()
        self.assertIn('M1_FIX_D2', text)
        self.assertIn('volatile descriptor_ptr_t gdtr;', text)
        self.assertIn('volatile descriptor_ptr_t idtr;', text)
        self.assertNotIn('const descriptor_ptr_t gdtr=', text)

    def test_flat_elf_contains_no_absolute_data_initializers(self):
        binary = ROOT / 'build/kernel64.elf'
        self.assertTrue(binary.is_file(), 'Run make check (builds ELF first)')
        checker = ROOT / 'tools' / 'verify_kernel64.py'
        self.assertTrue(checker.is_file())
        result = subprocess.run(['readelf', '-W', '-S', str(binary)],
                                capture_output=True, text=True, check=True).stdout
        import re
        pat = re.compile(r'^\s*\[\s*\d+\]\s+(\.data|\.got(?:\.\S+)?)\s+\S+\s+' r'[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\b')
        for line in result.splitlines():
            m = pat.match(line)
            if m:
                self.assertEqual(int(m.group(2), 16), 0, f'Unexpected initialized {m.group(1)} in kernel')


if __name__ == '__main__':
    unittest.main()
