#!/usr/bin/env python3
"""Sign the U1 terminal once per build and pin its complete IVX in the kernel.

Bootstrap-only: the kernel verifies a BLAKE3 pin, not a dynamic Ed25519
trust-store. A clean build produces a new key/pin; never reuse this policy
for general application installation.
"""
import argparse
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from installer.ivx_v1 import build_dev_terminal, public_raw, verify_dev_terminal
from installer.blake3_ref import blake3


def generate(program: bytes) -> tuple[bytes,bytes,str]:
    if len(program)<8 or len(program)>4080:
        raise ValueError('U1 program must fit one 4 KiB executable page')
    key=Ed25519PrivateKey.generate()
    ivx=build_dev_terminal(program,key)
    pub=public_raw(key)
    verified=verify_dev_terminal(ivx,pub)
    if verified.code[16:]!=program:
        raise ValueError('IVX signed developer profile verification failed')
    digest=blake3(ivx)
    c_array=', '.join('0x%02xu'%b for b in digest)
    header=('#ifndef INTELLECT_IVX_PIN_H\n#define INTELLECT_IVX_PIN_H\n'
            '#include <stdint.h>\n'
            f'#define INTELLECT_PINNED_IVX_BYTES {len(ivx)}u\n'
            f'static const uint8_t intellect_pinned_ivx_blake3[32] = {{{c_array}}};\n'
            '#endif\n')
    return ivx,pub,header


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--program',type=Path,required=True)
    p.add_argument('--ivx',type=Path,required=True)
    p.add_argument('--pub',type=Path,required=True)
    p.add_argument('--header',type=Path,required=True)
    a=p.parse_args()
    for output in (a.ivx,a.pub,a.header):
        output.parent.mkdir(parents=True,exist_ok=True)
    ivx,pub,header=generate(a.program.read_bytes())
    a.ivx.write_bytes(ivx)
    a.pub.write_bytes(pub)
    a.header.write_text(header)
    print('PASS: signed U1 developer IVX, host Ed25519 verified, kernel BLAKE3 pin generated')
    print('CAUTION: pinned initial boot profile only; not production trust-store')


if __name__=='__main__':main()
