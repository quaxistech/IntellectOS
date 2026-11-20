#!/usr/bin/env python3
"""Reject relocations and non-PIE-friendly sections in a flat U1 user image."""
from pathlib import Path
import re
import subprocess
import sys

def check(elf:Path, binary:Path):
    data=binary.read_bytes()
    if not 1<=len(data)<=4096:
        raise ValueError(f'code size {len(data)}, expected 1..4096 bytes')
    proc=subprocess.run(['readelf','-W','-S',str(elf)],check=True,capture_output=True,text=True)
    for line in proc.stdout.splitlines():
        m=re.search(r'\]\s+(\.\S+)\s+(\S+)',line)
        if not m:continue
        if m.group(1) not in ('.text','.symtab','.strtab','.shstrtab'):
            raise ValueError('unexpected section '+m.group(1))
    rel=subprocess.run(['readelf','-r',str(elf)],check=True,capture_output=True,text=True)
    if 'There are no relocations' not in rel.stdout:
        raise ValueError('user executable still has relocations')
    syms=subprocess.run(['nm','-n',str(elf)],check=True,capture_output=True,text=True)
    if not re.search(r'^0+\s+[A-Za-z]\s+_start$',syms.stdout,re.M):
        raise ValueError('user entry is not RVA zero')
    return len(data)
if __name__=='__main__':
    try:
        count=check(Path(sys.argv[1]),Path(sys.argv[2]))
        print(f'PASS: U1 flat image entry=0, bytes={count}, no relocations')
    except (IndexError,OSError,ValueError,subprocess.CalledProcessError) as e:
        raise SystemExit(f'FAIL: U1 image validation: {e}') from e
