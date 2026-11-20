#!/usr/bin/env python3
"""Fetch selected primary upstream driver sources into reference-only vendor tree.

The fetched code is NOT linked into IntellectOS. It has dependencies on its
original OS/UEFI and requires a port. Git SHAs are immutable upstream pins.
This is for audit and future native-driver development, not installation.
"""
from __future__ import annotations
import argparse
import pathlib
import tempfile
import urllib.request
import os

EDK="556dcfb466e9f05affeaff881d738807f8156d3c"
TINY="e7ff818dfdbb34a6d7083c921d661f14b1b8ba5f"
BSD="01a8cd5ebe9368fd115fb5060bcb1e60d1bbdf1f"
FILES=[
  ("tianocore", "edk2", EDK, "MdeModulePkg/Bus/Pci/PciBusDxe/PciBus.c"),
  ("tianocore", "edk2", EDK, "MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf"),
  ("tianocore", "edk2", EDK, "MdeModulePkg/Bus/Pci/XhciDxe/Xhci.c"),
  ("tianocore", "edk2", EDK, "MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf"),
  ("tianocore", "edk2", EDK, "MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpress.c"),
  ("tianocore", "edk2", EDK, "MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf"),
  ("tianocore", "edk2", EDK, "MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBus.c"),
  ("tianocore", "edk2", EDK, "MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf"),
  ("tianocore", "edk2", EDK, "License.txt"),
  ("hathach", "tinyusb", TINY, "src/host/usbh.c"),
  ("hathach", "tinyusb", TINY, "LICENSE"),
  ("freebsd", "freebsd-src", BSD, "sys/dev/usb/controller/xhci.c"),
  ("freebsd", "freebsd-src", BSD, "sys/dev/nvme/nvme.c"),
  ("freebsd", "freebsd-src", BSD, "COPYRIGHT"),
]

def main():
    ap=argparse.ArgumentParser()
    a=ap.add_mutually_exclusive_group(required=True)
    a.add_argument('--list',action='store_true')
    a.add_argument('--fetch',action='store_true')
    opt=ap.parse_args()
    target=pathlib.Path(__file__).resolve().parents[1]/"third_party/reference"
    for owner,repo,sha,path in FILES:
        remote=f"https://raw.githubusercontent.com/{owner}/{repo}/{sha}/{path}"
        dst=target/repo/path
        print(remote)
        if not opt.fetch: continue
        if dst.exists():
            print(f"  EXISTS: {dst}")
            continue
        with urllib.request.urlopen(urllib.request.Request(remote,
                   headers={'User-Agent':'IntellectOS-source-audit/1'}),timeout=20) as response:
            data=response.read(2*1024*1024+1)
        if not data or len(data)>2*1024*1024:
            raise SystemExit(f"invalid upstream source size: {path}")
        dst.parent.mkdir(parents=True,exist_ok=True)
        fd,temp=tempfile.mkstemp(dir=dst.parent,prefix='.vendor-')
        try:
            with os.fdopen(fd,'wb') as f:
                f.write(data);f.flush();os.fsync(f.fileno())
            os.replace(temp,dst)
        finally:
            if os.path.exists(temp):os.unlink(temp)
        print(f"  FETCHED ({len(data)} bytes): {dst}")
    print("REFERENCE SOURCES ONLY: not compiled, installed or executed by IntellectOS.")
if __name__=='__main__': main()
