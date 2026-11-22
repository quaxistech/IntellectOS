#!/usr/bin/env python3
"""End-to-End Test: Full USB-to-Disk Installation and Standalone Boot in QEMU.

Validates the full user request:
1. Installer takes target disk, performs wiping/cleaning.
2. Partitions GPT: 512 MiB FAT32 ESP + IntellectFS v1 root partition.
3. Formats ESP with BOOTX64.EFI, formats IntellectFS with /sbin/terminal.ivx.
4. Boots QEMU with ONLY the installed target disk (no installer USB attached).
5. Verifies OVMF boots into BOOTX64.EFI, mounts IFS-1 root, enters Ring 3 terminal.
6. Exercises user terminal commands including drivers, disk, and install.
"""
from __future__ import annotations
import argparse
import errno
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from installer.target_disk_installer import install_target_disk


def firmware():
    for code, vars in (('/usr/share/OVMF/OVMF_CODE_4M.fd', '/usr/share/OVMF/OVMF_VARS_4M.fd'),
                       ('/usr/share/OVMF/OVMF_CODE.fd', '/usr/share/OVMF/OVMF_VARS.fd')):
        if Path(code).is_file() and Path(vars).is_file():
            return code, vars
    raise RuntimeError('OVMF firmware unavailable (sudo apt install ovmf)')


def run_standalone_boot_test(target_img: Path, timeout: int = 140) -> None:
    code, vars = firmware()
    store = ROOT / 'build' / 'target-disk-qemu'
    store.mkdir(parents=True, exist_ok=True)
    varcopy = store / 'OVMF_VARS_TARGET.fd'
    varcopy.write_bytes(Path(vars).read_bytes())

    cmd = [
        'qemu-system-x86_64', '-m', '256M', '-net', 'none', '-monitor', 'none',
        '-no-reboot', '-display', 'none', '-serial', 'stdio',
        '-drive', f'if=pflash,format=raw,readonly=on,file={code}',
        '-drive', f'if=pflash,format=raw,file={varcopy}',
        '-drive', f'if=ide,format=raw,file={target_img.resolve()}'
    ]

    master, slave = pty.openpty()
    child = None
    output = ''
    try:
        child = subprocess.Popen(cmd, cwd=ROOT, stdin=slave, stdout=slave, stderr=slave,
                                close_fds=True, start_new_session=True)
        os.close(slave)
        slave = -1

        def until(expected: str, deadline: float):
            nonlocal output
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise RuntimeError(f'QEMU terminated ({child.returncode})')
                rd, _, _ = select.select([master], [], [], 0.25)
                if rd:
                    try:
                        data = os.read(master, 32768)
                    except OSError as e:
                        if e.errno == errno.EIO:
                            raise RuntimeError('serial PTY closed') from e
                        raise
                    if not data:
                        raise RuntimeError('serial channel closed')
                    output += data.decode('utf-8', 'replace').replace('\r\n', '\n').replace('\r', '\n')
                    if len(output) > 800000:
                        raise RuntimeError('output limit')
                    if expected in output:
                        return
            raise RuntimeError('timeout waiting for ' + repr(expected))

        # 1. Handoff to kernel and IFS verification
        until('IFS-1: SEALED SYSTEM ROOT VERIFIED', time.monotonic() + timeout)
        until('IFS-1: VERIFIED PINNED IVX FROM DISK', time.monotonic() + 20)
        until('IFS-1: timezone UTC', time.monotonic() + 20)
        until('U1: Ring3 user address space installed', time.monotonic() + 20)
        until('intellect:/$ ', time.monotonic() + 15)

        # 2. Test help command
        os.write(master, b'help\r')
        until('COMMANDS: help about pwd echo <text> mem clear drivers disk install exit', time.monotonic() + 20)

        # 3. Test drivers command (USB & storage drivers)
        os.write(master, b'drivers\r')
        until('USB: xHCI, EHCI, UHCI controllers & Mass Storage active', time.monotonic() + 20)

        # 4. Test disk command
        os.write(master, b'disk\r')
        until('STORAGE: UEFI Block Devices, GPT Partition Table', time.monotonic() + 20)

        # 5. Test install command
        os.write(master, b'install\r')
        until('INSTALLER: IntellectOS USB-to-Disk Installer ready', time.monotonic() + 20)

        # 6. Test echo command
        os.write(master, b'echo USB_TO_DISK_INSTALL_VERIFIED\r')
        until('\nUSB_TO_DISK_INSTALL_VERIFIED\n', time.monotonic() + 20)

        # 7. Test exit command
        os.write(master, b'exit\r')
        until('U1: user terminal exited', time.monotonic() + 20)

        print('PASS: USB-to-disk installation verified (clean wipe, GPT, ESP FAT32, IFS-1 root, standalone boot)')

    except (RuntimeError, OSError) as e:
        print('FAIL: ' + str(e), file=sys.stderr)
        print('---- Real QEMU serial output tail ----\n' + output[-12000:], file=sys.stderr)
        raise
    finally:
        if child and child.poll() is None:
            try:
                os.killpg(child.pid, signal.SIGTERM)
                child.wait(timeout=5)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait(timeout=5)
        os.close(master)
        if slave >= 0:
            os.close(slave)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target-image', type=Path, default=ROOT / 'build/installed-target-disk.img')
    p.add_argument('--timeout', type=int, default=140)
    args = p.parse_args()

    efi_path = ROOT / 'build/uefi/BOOTX64.EFI'
    ivx_path = ROOT / 'build/user/terminal.ivx'
    pub_path = ROOT / 'build/user/terminal.pub'

    if not efi_path.is_file() or not ivx_path.is_file() or not pub_path.is_file():
        raise RuntimeError("Missing build assets; run 'make' first")

    print(f"Step 1: Installing to target disk {args.target_image} (wipe + partition + format ESP & IFS)...")
    install_target_disk(
        target=args.target_image,
        efi_bytes=efi_path.read_bytes(),
        ivx_bytes=ivx_path.read_bytes(),
        pub_bytes=pub_path.read_bytes(),
        size_gib=1,
        wipe=True,
        timezone='UTC'
    )
    print(f"Step 1 complete: Target disk {args.target_image} created and verified.")

    print("Step 2: Booting installed target disk standalone in QEMU (no installer USB attached)...")
    run_standalone_boot_test(args.target_image, timeout=args.timeout)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
