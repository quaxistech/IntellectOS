"""IFS-1 bootstrap profile tests: real sparse volume, golden checks, corruption."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from installer.ifs_disk_v1 import (Volume, IFSError, crc32c, check_bootstrap,
                                 make_bootstrap, make_seal, check_seal,
                                 ANCHOR_START, DESCRIPTOR_PBLK, BLOCK)

class IFSTests(unittest.TestCase):
    def setUp(self):
        self.d=tempfile.TemporaryDirectory()
        self.addCleanup(self.d.cleanup)
        self.path=Path(self.d.name)/'test.img'
        with self.path.open('xb') as f:f.truncate(256*1024*1024)
        self.vol=Volume(self.path,0,self.path.stat().st_size)
        self.data={'/sbin/init.ivx':b'IVX1'+bytes(range(256))*24,
                   '/etc/timezone':b'Europe/Kyiv\n',
                   '/etc/shadow':b'root:$argon2id$v=19$example\n',
                   '/empty':b''}
        self.vol.format(self.data)
    def flip(self,pblk:int,offset:int):
        with self.path.open('r+b') as f:
            f.seek(pblk*BLOCK+offset);x=f.read(1)
            f.seek(pblk*BLOCK+offset);f.write(bytes([x[0]^0x1]))
    def test_valid_readback(self):
        actual=self.vol.check()
        self.assertEqual({k:v.data for k,v in actual.items() if v.type=='file'},self.data)
        self.assertEqual(actual['/'].type,'dir')
        self.assertEqual(actual['/etc'].type,'dir')
    def test_crc32c_authoritative_vector(self):
        self.assertEqual(crc32c(b'123456789'),0xe3069283)
    def test_damage_data_rejected(self):
        self.flip(16384,0)
        with self.assertRaisesRegex(IFSError,'pointer digest'):self.vol.check()
    def test_damage_seal_rejected(self):
        self.flip(ANCHOR_START,216)
        with self.assertRaisesRegex(IFSError,'CRC32C'):self.vol.check()
    def test_damage_descriptor_rejected(self):
        self.flip(DESCRIPTOR_PBLK,88)
        with self.assertRaises(IFSError):self.vol.check()
    def test_one_bad_bootstrap_can_recover(self):
        self.flip(1,128)
        self.assertEqual(self.vol.check()['/etc/timezone'].data,b'Europe/Kyiv\n')
    def test_all_bootstrap_corrupt_fail(self):
        for pblk in (1,2,self.vol.blocks-3,self.vol.blocks-2):self.flip(pblk,128)
        with self.assertRaisesRegex(IFSError,'no valid bootstrap'):self.vol.check()
    def test_node_bad_digest(self):
        self.flip(256,64)
        with self.assertRaises(IFSError):self.vol.check()
    def test_zero_size_not_allocated(self):
        self.assertEqual(self.vol.check()['/empty'].data,b'')
    def test_reject_corrupt_geometry(self):
        b=self.vol._read(1)
        with self.assertRaises(IFSError):check_bootstrap(b,self.vol.blocks+1,512)
        with self.assertRaises(IFSError):check_bootstrap(b,self.vol.blocks,4096)
    def test_reject_unaligned_partition(self):
        with self.assertRaises(IFSError):Volume(self.path,1,self.path.stat().st_size-1)

if __name__=='__main__':unittest.main()
