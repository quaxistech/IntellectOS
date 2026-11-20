#!/usr/bin/env python3
"""Build a signed IVX-1 *developer fixture*. NOT bootable by current U1.
No private signing keys are bundled; key is generated per invocation.
"""
import argparse,sys
from pathlib import Path
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from installer.ivx_v1 import build_dev_terminal,public_raw,verify_dev_terminal

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--terminal-bin',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
if not a.terminal_bin.is_file():raise SystemExit('terminal binary not built')
if a.out.exists() or a.out.with_suffix('.pub').exists():raise SystemExit('refuse overwrite')
priv=Ed25519PrivateKey.generate()
ivx=build_dev_terminal(a.terminal_bin.read_bytes(),priv)
pub=public_raw(priv)
assert verify_dev_terminal(ivx,pub).code[16:]==a.terminal_bin.read_bytes()
a.out.write_bytes(ivx)
a.out.with_suffix('.pub').write_bytes(pub)
print(f'PASS: signed IVX structural fixture: {a.out} ({len(ivx)} bytes)')
print('WARNING: trust key file beside IVX is NOT production trust; current kernel cannot verify or load this format')
