#!/usr/bin/env python3
"""Independently check a candidate installed IFS-1 boot image, without writing it.

This is a host verifier, not an in-kernel authentication/IVX loader.
It checks the GPT/FAT32 container and the IFS sealed files, the declared
root password KDF record, and the detached Ed25519 developer IVX signature.
"""
from __future__ import annotations
import argparse
import getpass
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from installer import boot_disk_v1 as disk
from installer.ivx_v1 import verify_dev_terminal
from installer.system_config_v1 import validate_timezone, verify_password, ConfigError
from tools.make_ifs_install_image import IFS_GPT_GUID, volume_for_image


def verify(path: Path, *, efi: bytes | None = None, password: str | None = None) -> dict:
    if not path.is_file():
        raise ValueError('disk image must be a regular file')
    # The base builder models the second GPT type as an unformatted reservation.
    # Override that type solely while verifying the IFS-specific image.
    original = disk.DATA_RESERVED_TYPE
    try:
        disk.DATA_RESERVED_TYPE = IFS_GPT_GUID
        disk.verify_image(path, efi)  # validates both GPT headers and FAT32
    finally:
        disk.DATA_RESERVED_TYPE = original
    volume = volume_for_image(path)
    items = volume.check()
    required = ('/etc/system.json', '/etc/timezone', '/etc/trust/developer.pub',
                '/sbin/terminal.ivx')
    for name in required:
        if name not in items or items[name].data is None:
            raise ValueError(f'missing required IFS system file: {name}')
    config_bytes = items['/etc/system.json'].data
    root = json.loads(config_bytes)
    if root.get('schema') != 'IntellectOS.system_config.v1' or root.get('rtc_policy') != 'UTC':
        raise ValueError('invalid IFS system config schema')
    if len(root.get('users', [])) != 1 or root['users'][0].get('name') != 'root' or root['users'][0].get('uid') != 0:
        raise ValueError('exact root identity not configured')
    tz = validate_timezone(root.get('timezone_iana'))
    if items['/etc/timezone'].data != (tz + '\n').encode('utf-8'):
        raise ValueError('timezone files disagree')
    kdf = root['users'][0].get('password_kdf', {})
    if (kdf.get('kind') != 'Argon2id' or kdf.get('version') != 19 or
        not isinstance(kdf.get('memory_kib'), int) or kdf['memory_kib'] < 65536 or
        not isinstance(kdf.get('iterations'), int) or kdf['iterations'] < 2 or
        kdf.get('parallelism') != 1):
        raise ValueError('root Argon2id record invalid')
    if password is not None and not verify_password(root['users'][0], password):
        raise ValueError('root password rejected')
    ivx = items['/sbin/terminal.ivx'].data
    pub = items['/etc/trust/developer.pub'].data
    verified = verify_dev_terminal(ivx, pub)
    return {'image': str(path), 'timezone': tz,
            'root_password': 'verified' if password is not None else 'record-present-not-tested',
            'ivx_signed': True, 'ivx_code_bytes': len(verified.code),
            'system_files': sorted(n for n,a in items.items() if a.data is not None),
            'filesystem': 'IFS-1 initial sealed profile',
            'runtime_status': 'NOT_PROVEN: root login / IVX exec in kernel are separate gates'}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('image', type=Path)
    p.add_argument('--efi', type=Path, default=None,
                   help='compare installed BOOTX64.EFI bytes with this file')
    p.add_argument('--verify-root', action='store_true',
                   help='prompt for password securely and verify Argon2id on host')
    args = p.parse_args()
    password = getpass.getpass('IntellectOS root password: ') if args.verify_root else None
    try:
        data = verify(args.image, efi=args.efi.read_bytes() if args.efi else None,
                      password=password)
    except (ValueError, ConfigError, OSError) as exc:
        p.exit(1, f'FAIL: installed system verification: {exc}\n')
    print(json.dumps(data, indent=2, ensure_ascii=False))
    print('PASS: GPT/ESP + IFS-1 seal + system config + signed IVX (host verification)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
