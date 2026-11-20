#!/usr/bin/env python3
"""Create a UEFI bootable *preinstall* GPT/FAT32 image from existing EFI.
This is not a completed IntellectFS installation. Never writes block devices.
"""
from __future__ import annotations
import argparse
import json
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from installer.boot_disk_v1 import build_image,verify_image,GiB,DiskFormatError


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--efi',type=Path,required=True,
                    help='already built build/uefi/BOOTX64.EFI from U1')
    ap.add_argument('--output',type=Path,required=True,help='new regular .img file')
    ap.add_argument('--size-gib',type=int,default=1,choices=range(1,129),metavar='1..128')
    opt=ap.parse_args()
    if not opt.efi.is_file():raise SystemExit('EFI not found; compile existing U1 first')
    blob=opt.efi.read_bytes()
    try:
        result=build_image(opt.output,blob,opt.size_gib*GiB)
        check=verify_image(opt.output,blob)
    except (DiskFormatError,OSError) as exc:
        raise SystemExit('BLOCKED: '+str(exc)) from exc
    print(json.dumps({'created':result,'verified':check},ensure_ascii=False,indent=2))
    print('NOTE: system partition is RESERVED / UNFORMATTED; NO IntellectFS or root login installed.')

if __name__=='__main__':main()
