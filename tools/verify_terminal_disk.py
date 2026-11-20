#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from installer.boot_disk_v1 import verify_image,DiskFormatError
p=argparse.ArgumentParser()
p.add_argument('image',type=Path)
p.add_argument('--efi',type=Path)
a=p.parse_args()
try: print(json.dumps(verify_image(a.image,a.efi.read_bytes() if a.efi else None),indent=2))
except (OSError,DiskFormatError) as e:raise SystemExit('FAIL: '+str(e)) from e
