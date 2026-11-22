"""Acceptance regression for the host-installed IFS image and the root KDF.

Never accesses block devices; all writes are to temporary regular files.
"""
from pathlib import Path
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from installer.ivx_v1 import build_dev_terminal, public_raw
from installer.system_config_v1 import provision, canonical_json
from tools.make_ifs_install_image import create
from tools.verify_installed_system import verify


class TestInstalledSystemVerifier(unittest.TestCase):
    def test_timezone_does_not_require_argon2_python_module(self):
        result = subprocess.run([sys.executable, '-S', '-c',
             'from installer.system_config_v1 import validate_timezone; print(validate_timezone("Etc/UTC"))'],
             cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'Etc/UTC')

    def test_provisioning_fails_explicitly_without_argon2(self):
        result = subprocess.run([sys.executable, '-S', '-c',
             'from installer.system_config_v1 import provision; provision("long_secure_password", "Etc/UTC")'],
             cwd=ROOT, text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Argon2id backend is missing', result.stderr)

    @unittest.skipUnless(importlib.util.find_spec("argon2"), "Argon2id host backend not installed")
    def test_image_root_password_and_ivx_signature_verified(self):
        password='IntellectOS-Test-Passphrase-Long-123!'
        with tempfile.TemporaryDirectory() as td:
            img=Path(td)/'installed.img'
            efi=b'MZ'+bytes(1022)
            key=Ed25519PrivateKey.generate()
            pub=public_raw(key)
            ivx=build_dev_terminal(b'\x90'*64, key)
            cfg=canonical_json(provision(password,'Etc/UTC',memory_kib=65536,iterations=2))
            create(img,efi,ivx,pub,cfg,1)
            info=verify(img,efi=efi,password=password)
            self.assertEqual(info['root_password'],'verified')
            self.assertTrue(info['ivx_signed'])
            with self.assertRaisesRegex(ValueError,'root password rejected'):
                verify(img, password='a completely different password')


if __name__ == '__main__':
    unittest.main()
