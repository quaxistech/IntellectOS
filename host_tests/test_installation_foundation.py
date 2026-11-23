"""Negative fixtures, two GPT copies, VFS file reads, IVX trust and root record."""
from __future__ import annotations
import tempfile
from pathlib import Path
import subprocess
import os
import sys
import unittest
import struct
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from installer.blake3_ref import blake3
from installer.boot_disk_v1 import (GiB,build_image,verify_image,DiskFormatError)
from installer.ivx_v1 import (build_dev_terminal,verify_dev_terminal,IVXError,public_raw)
from installer.system_config_v1 import provision,verify_password,validate_timezone,ConfigError,canonical_json

class ImageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.efi=b'MZ'+bytes(range(251))*51
        self.efi_path=self.root/'BOOTX64.EFI'
        self.efi_path.write_bytes(self.efi)
        self.disk=self.root/'disk.img'
        build_image(self.disk,self.efi,GiB)
    def tearDown(self):self.temp.cleanup()
    def test_dual_gpt_and_fat(self):
        s=verify_image(self.disk,self.efi)
        self.assertEqual(s['gpt'],'PRIMARY_AND_BACKUP_VALID')
        self.assertEqual(s['ifs'],'RESERVED_NOT_FORMATTED')
    def test_refuses_overwrite(self):
        with self.assertRaises(DiskFormatError):build_image(self.disk,self.efi,GiB)
    def test_refuses_bad_header_crc(self):
        with self.disk.open('r+b') as f:f.seek(512+16);f.write(b'BAD!')
        with self.assertRaisesRegex(DiskFormatError,'CRC'):
            verify_image(self.disk,self.efi)
    def test_refuses_backup_header_crc(self):
        with self.disk.open('r+b') as f:f.seek(GiB-512+16);f.write(b'BAD!')
        with self.assertRaisesRegex(DiskFormatError,'CRC'):
            verify_image(self.disk,self.efi)
    def test_refuses_tampered_file(self):
        # The host verifier can compare the actual EFI bytes, independently of filenames.
        with self.assertRaisesRegex(DiskFormatError,'differs'):
            verify_image(self.disk,self.efi+b'wrong')
    def test_freestanding_fat32_backend(self):
        out=self.root/'vfs-test'
        subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-O2',
            '-Ikernel/include','kernel/src64/fat32_vfs64.c',
            'host_tests/fat32_host.c','-o',str(out)],cwd=ROOT,check=True,
            capture_output=True,text=True)
        done=subprocess.run([str(out),str(self.disk),str(self.efi_path)],check=True,
                            capture_output=True,text=True)
        self.assertIn('PASS: native freestanding FAT32 VFS',done.stdout)
    def test_nonempty_regular_file_refused(self):
        a=self.root/'not-empty.img';a.write_text('protect me')
        with self.assertRaises(DiskFormatError):build_image(a,self.efi,GiB)

class IVXTests(unittest.TestCase):
    def test_blake3_independent_vectors(self):
        self.assertEqual(blake3(b'').hex(),
                         'af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262')
        self.assertEqual(blake3(b'abc').hex(),
                         '6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85')
        # Official BLAKE3-team/BLAKE3 vectors (input i % 251); multi-chunk tree.
        self.assertEqual(blake3(bytes(i%251 for i in range(1025))).hex(),
                         'd00278ae47eb27b34faecf67b4fe263f82d5412916c1ffd97c8cb7fb814b8444')
        self.assertEqual(blake3(bytes(i%251 for i in range(4096))).hex(),
                         '015094013f57a5277b59d8475c0501042c0b642e531b0a1c8f58d2163229e969')
    def test_valid_signed_fixture_and_no_trust_bypass(self):
        key=Ed25519PrivateKey.generate()
        source=bytes(range(251))*6
        blob=build_dev_terminal(source,key)
        item=verify_dev_terminal(blob,public_raw(key))
        self.assertEqual(item.code[16:],source)
        other=Ed25519PrivateKey.generate()
        with self.assertRaises(IVXError):verify_dev_terminal(blob,public_raw(other))
    def test_content_tamper_and_signature_tamper(self):
        key=Ed25519PrivateKey.generate()
        blob=build_dev_terminal(b'\x90\xc3',key)
        for pos in (len(blob)-1,192+16,96,16):
            buf=bytearray(blob);buf[pos]^=1
            with self.subTest(offset=pos),self.assertRaises(IVXError):
                verify_dev_terminal(bytes(buf),public_raw(key))
    def test_rejects_overflow_and_wx(self):
        key=Ed25519PrivateKey.generate()
        blob=bytearray(build_dev_terminal(b'\x90\xc3',key))
        struct.pack_into('<Q',blob,136,2**64-1)
        with self.assertRaises(IVXError):verify_dev_terminal(bytes(blob),public_raw(key))
        blob=bytearray(build_dev_terminal(b'\x90\xc3',key))
        struct.pack_into('<I',blob,168,7) # RWX, fail before signature
        with self.assertRaises(IVXError):verify_dev_terminal(bytes(blob),public_raw(key))

class CredentialTests(unittest.TestCase):
    def test_root_hash_and_timezone(self):
        record=provision('verylong_correct_9?password','UTC')
        self.assertEqual(record['users'][0]['uid'],0)
        self.assertNotIn('verylong_correct_9?password',canonical_json(record).decode())
        self.assertTrue(verify_password(record['users'][0],'verylong_correct_9?password'))
        self.assertFalse(verify_password(record['users'][0],'incorrect-password'))
    def test_reject_timezone_traversal_and_invalid_password(self):
        for name in ('../../etc/shadow','Some/Nonexistent_Zone',''):
            with self.subTest(name=name),self.assertRaises(ConfigError):validate_timezone(name)
        with self.assertRaises(ConfigError):provision('a'*257,'UTC')
        with self.assertRaises(ConfigError):provision('bad\0pass','UTC')

if __name__=='__main__':unittest.main()

# This additional class is picked up by unittest discover even if placed
# after the standalone __main__ clause (which is false for discovery imports).
from installer.ifs_bootstrap_v1 import parse_bootstrap,inspect_four,IFSBootstrapError
import uuid

class IFSReadOnlyTests(unittest.TestCase):
    @staticmethod
    def fixture():
        b=bytearray(4096)
        b[:8]=b'IFSB0001'
        struct.pack_into('<HHHH',b,8,1,0,256,12)
        b[16:32]=uuid.UUID('4d1c6a84-c6a1-4542-b06c-6da0a3d5c51e').bytes
        struct.pack_into('<QQQQ',b,32,65536,9832,1,0)
        struct.pack_into('<QIIQQ',b,64,3,64,1,80,100)
        struct.pack_into('<III',b,104,0,0,0)
        struct.pack_into('<III',b,116,512,1024,1)
        from installer.blake3_ref import blake3
        b[128:160]=blake3(b'IFS/bootstrap/v1\0'+b)
        return bytes(b)
    def test_exact_bootstrap_layout_and_digest(self):
        b=self.fixture()
        self.assertEqual(parse_bootstrap(b,65536).volume_blocks,65536)
        valid=inspect_four(lambda _:b,65536)
        self.assertEqual(len(valid),4)
    def test_bootstrap_torn_and_split_brain_rejected(self):
        b=bytearray(self.fixture());b[80]^=1
        with self.assertRaises(IFSBootstrapError):parse_bootstrap(bytes(b),65536)
        b=bytearray(self.fixture());b[160]=1
        with self.assertRaises(IFSBootstrapError):parse_bootstrap(bytes(b),65536)
