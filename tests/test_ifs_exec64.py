"""Verify file-backed Ring3 bootstrap selection never accepts modified IVX bytes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.make_pinned_dev_ivx import generate
from tools.make_ifs_install_image import verify_kernel_pin


class TestIFSExec64(unittest.TestCase):
    def test_pinned_ivx_code_loaded_and_tamper_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td)
            image,pub,header=generate(bytes(range(1,129)))
            (d/'ifs_ivx_pin.h').write_text(header)
            (d/'terminal.ivx').write_bytes(image)
            verify_kernel_pin(d/'ifs_ivx_pin.h',image)
            with self.assertRaisesRegex(ValueError,'pin length|does not match kernel'):
                verify_kernel_pin(d/'ifs_ivx_pin.h',image+b'X')
            target=d/'ifs-exec-host'
            command=['gcc','-std=c11','-Wall','-Wextra','-Werror','-O2',
                     '-Ikernel/include','-I'+str(d),
                     'kernel/src64/ifs_blake3_v1.c','kernel/src64/ifs_exec64.c',
                     'tests/ifs_exec64_host.c','-o',str(target)]
            subprocess.run(command,cwd=ROOT,check=True,capture_output=True,text=True)
            result=subprocess.run([str(target),str(d/'terminal.ivx')],
                cwd=ROOT,check=True,capture_output=True,text=True)
            self.assertIn('PASS: pinned IFS external IVX loader',result.stdout)


if __name__ == '__main__':unittest.main()
