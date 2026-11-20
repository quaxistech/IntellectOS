"""Host U1 privilege boundaries and user binary invariants."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class TestU1Terminal(unittest.TestCase):
    def test_user_pointer_bounds(self):
        for flags in ('-O0','-O2'):
            with tempfile.TemporaryDirectory() as td:
                exe=str(Path(td)/'userpolicy')
                subprocess.run(['gcc','-std=c11',flags,'-Wall','-Wextra','-Werror',
                                '-Ikernel/include',
                                'kernel/src64/userpolicy64.c','tests/userpolicy64_host.c',
                                '-o',exe],cwd=ROOT,check=True,capture_output=True,text=True)
                proc=subprocess.run([exe],check=True,capture_output=True,text=True)
                self.assertIn('PASS: U1 user pointer',proc.stdout)
    def test_framebuffer_bounds(self):
        for flags in ('-O0','-O2'):
            with tempfile.TemporaryDirectory() as td:
                exe=str(Path(td)/'terminalfb')
                subprocess.run(['gcc','-std=c11',flags,'-Wall','-Wextra','-Werror',
                                '-Ikernel/include','-Ikernel/src64',
                                'kernel/src64/terminalfb64.c','tests/terminalfb64_host.c',
                                '-o',exe],cwd=ROOT,check=True,capture_output=True,text=True)
                proc=subprocess.run([exe],check=True,capture_output=True,text=True)
                self.assertIn('PASS: U1 framebuffer guard',proc.stdout)
    def test_kernel_trap_guards(self):
        c=(ROOT/'kernel/src64/userterminal64.c').read_text()
        g=(ROOT/'kernel/x86_64/usergate64.S').read_text()
        e=(ROOT/'kernel/src64/exceptions64.c').read_text()
        self.assertIn('intellect_u1_user_range(f->rsi,f->rdx)',c)
        self.assertIn('pt[0]=owned[3]|P|U;',c)
        self.assertIn('pt[1]=owned[4]|P|W|U|NX;',c)
        self.assertIn('iretq',g)
        self.assertIn('idt[128].type_attr=UINT8_C(0xEE)',e)
    def test_user_image(self):
        subprocess.run(['python3','tools/verify_user_terminal.py',
                        'build/user/terminal.elf','build/user/terminal.bin'],
                        cwd=ROOT,check=True,capture_output=True)
if __name__=='__main__':unittest.main()
