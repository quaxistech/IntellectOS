#!/usr/bin/env python3
"""Independent policy gate for the flat, location-independent x86-64 kernel.

We do not run an ELF loader at boot: the flat payload must have a zero entry,
no relocations, no implicit runtime, and a bounded footprint.
"""
import pathlib
import subprocess
import sys


def verify(elf: pathlib.Path, binary: pathlib.Path):
    result = subprocess.run(['readelf', '-h', '-S', '-r', '-s', str(elf)],
                            check=True, capture_output=True, text=True).stdout
    conditions = {
        '64-bit ELF': 'Class:                             ELF64' in result,
        'x86-64 machine': 'Advanced Micro Devices X86-64' in result,
        'entry at 0': 'Entry point address:               0x0' in result,
        'entry symbol': 'intellect64_entry' in result,
        'no relocations': 'There are no relocations in this file.' in result,
        'no GOT or dynamic loader': all(x not in result for x in
            ('.got ', '.plt ', '.dynamic ', '.interp ', '.rela.')),
        'bounded image': 128 <= binary.stat().st_size <= 32 * 4096,
        'not an ELF header': not binary.read_bytes().startswith(b'\x7fELF'),
    }
    for label, okay in conditions.items():
        if not okay:
            raise SystemExit('FAIL: kernel64 ' + label)
    print('PASS: flat native kernel ELF64, zero-relative entry, no relocation or runtime dependencies')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: verify_kernel64.py kernel64.elf kernel64.bin')
    verify(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
