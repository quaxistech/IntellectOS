#!/usr/bin/env python3
"""IntellectOS Target Disk Installer CLI.

Installs IntellectOS from USB / build assets onto a target disk (or disk image):
- Safe target disk wiping/cleaning
- Standard GPT partitioning (512 MiB ESP + IntellectFS v1 root partition)
- FAT32 ESP formatting with BOOTX64.EFI
- IntellectFS v1 volume formatting with /sbin/terminal.ivx and root configuration
- Bypass password requirement (no passwords needed for now)
- Full independent verification of installed system
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from installer.target_disk_installer import install_target_disk, TargetSafetyError, InstallationError
from installer.boot_disk_v1 import DiskFormatError


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target', type=Path, required=True,
                   help='Target disk image file or block device (e.g. build/target-disk.img)')
    p.add_argument('--size-gib', type=int, default=1,
                   help='Target disk size in GiB when creating or expanding (default: 1, min: 1)')
    p.add_argument('--wipe', action='store_true', default=True,
                   help='Clean/wipe target disk before partitioning (default: True)')
    p.add_argument('--no-wipe', action='store_false', dest='wipe',
                   help='Do not wipe target disk before partitioning')
    p.add_argument('--efi', type=Path, default=ROOT / 'build/uefi/BOOTX64.EFI',
                   help='Path to source BOOTX64.EFI')
    p.add_argument('--ivx', type=Path, default=ROOT / 'build/user/terminal.ivx',
                   help='Path to source terminal.ivx')
    p.add_argument('--pub', type=Path, default=ROOT / 'build/user/terminal.pub',
                   help='Path to developer.pub')
    p.add_argument('--timezone', default='UTC',
                   help='System timezone (default: UTC)')
    p.add_argument('--allow-real-usb', action='store_true',
                   help='Allow installing to a real USB block device (requires --confirm)')
    p.add_argument('--confirm', default=None,
                   help="Confirmation token for real USB install (must be 'INSTALL /dev/sdX')")
    args = p.parse_args()

    # Locate assets
    if not args.efi.is_file():
        p.exit(1, f"ERROR: Missing EFI bootloader binary at {args.efi} (run 'make' first)\n")
    if not args.ivx.is_file():
        p.exit(1, f"ERROR: Missing terminal IVX binary at {args.ivx} (run 'make' first)\n")
    if not args.pub.is_file():
        p.exit(1, f"ERROR: Missing public key at {args.pub} (run 'make' first)\n")

    efi_bytes = args.efi.read_bytes()
    ivx_bytes = args.ivx.read_bytes()
    pub_bytes = args.pub.read_bytes()

    print(f"IntellectOS Disk Installer")
    print(f"Target: {args.target}")
    print(f"Wipe: {args.wipe}")
    print(f"Size: {args.size_gib} GiB")
    print(f"Timezone: {args.timezone}")
    print(f"Password: none required (direct terminal boot)")

    try:
        receipt = install_target_disk(
            target=args.target,
            efi_bytes=efi_bytes,
            ivx_bytes=ivx_bytes,
            pub_bytes=pub_bytes,
            size_gib=args.size_gib,
            wipe=args.wipe,
            timezone=args.timezone,
            allow_real_usb=args.allow_real_usb,
            confirm_token=args.confirm
        )
        print("\nInstallation Successful!")
        print(json.dumps(receipt, indent=2))
        return 0
    except (TargetSafetyError, InstallationError, DiskFormatError, ValueError) as exc:
        print(f"\nINSTALLATION FAILED: {exc}", file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
