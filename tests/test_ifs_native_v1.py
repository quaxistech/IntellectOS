"""Cross-language IFS-1: actual disk layout from Python, native C parser."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import sys
import uuid

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from installer.ifs_disk_v1 import Volume
from installer.blake3_ref import blake3

class NativeIFSReadTests(unittest.TestCase):
    def test_portable_c_blake3_matches_python(self):
        with tempfile.TemporaryDirectory() as td:
            exe=Path(td)/'hash'
            cmd=['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',
                 '-Ikernel/include','kernel/src64/ifs_blake3_v1.c',
                 'tests/ifs_blake3_host.c','-o',str(exe)]
            subprocess.run(cmd,cwd=ROOT,check=True,capture_output=True,text=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
            vectors=dict(line.split() for line in result.stdout.splitlines())
            for n in (0,1,2,3,4,5,6,63,64,1024,1025,2048,4096):
                data=bytes(i%251 for i in range(n))
                self.assertEqual(vectors[str(n)],blake3(data).hex())
    def test_native_c_reads_ifs_inside_real_gpt(self):
        from installer.boot_disk_v1 import build_image, GiB
        from tools.make_ifs_install_image import volume_for_image,patch_partition_type,IFS_GPT_GUID
        with tempfile.TemporaryDirectory() as td:
            img=Path(td)/'ifs.img'
            build_image(img,b'MZ'+bytes(510),GiB)
            v=volume_for_image(img)
            v.format({'/etc/timezone':b'Etc/UTC\n','/sbin/terminal.ivx':b'IVX test payload'})
            patch_partition_type(img,IFS_GPT_GUID)
            exe=Path(td)/'ifs_reader'
            cmd=['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Ikernel/include',
                 'kernel/src64/ifs_blake3_v1.c','kernel/src64/ifs_ro64.c',
                 'tests/ifs_ro64_host.c','-o',str(exe)]
            subprocess.run(cmd,cwd=ROOT,check=True,capture_output=True,text=True)
            good=subprocess.run([str(exe),str(img)],capture_output=True,text=True)
            self.assertEqual(good.returncode,0,good.stderr)
            self.assertEqual(good.stdout,'Etc/UTC\n')
            # Corrupt the seal: independent C reader must reject mount.
            with img.open('r+b') as f:
                f.seek(v.start+3*4096+216);old=f.read(1)
                f.seek(v.start+3*4096+216);f.write(bytes([old[0]^1]))
            damaged=subprocess.run([str(exe),str(img)],capture_output=True,text=True)
            self.assertNotEqual(damaged.returncode,0)

if __name__=='__main__':unittest.main()
