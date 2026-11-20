#!/usr/bin/env python3
"""M3-A non-destructive QEMU+OVMF+COM1 acceptance probe.

Uses a PTY so QEMU's '-serial stdio' works as if attached to a terminal.
Runs the existing C1 launcher and sends a few bounded diagnostics. Does not
open physical disks and never calls dd. All output comes from guest firmware
and the running kernel, not from an emulator of the C1 command parser.
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

ROOT = Path(__file__).resolve().parent.parent

class Session:
    def __init__(self, fd: int, child: subprocess.Popen):
        self.fd, self.child = fd, child
        self.buf = ''
        self.offset = 0

    def expect(self, needle: str, timeout: float):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            idx = self.buf.find(needle, self.offset)
            if idx >= 0:
                self.offset = idx + len(needle)
                return
            if self.child.poll() is not None:
                raise RuntimeError(f'QEMU terminated early ({self.child.returncode}), waiting for {needle!r}')
            wait = min(0.3, max(0.0, end-time.monotonic()))
            ready, _, _ = select.select([self.fd], [], [], wait)
            if ready:
                try:
                    chunk = os.read(self.fd, 65536)
                except OSError as e:
                    if e.errno == errno.EIO:
                        raise RuntimeError('PTY closed unexpectedly') from e
                    raise
                if not chunk:
                    raise RuntimeError('serial console unexpectedly closed')
                self.buf += chunk.decode('utf-8', errors='replace').replace('\r\n', '\n').replace('\r', '\n')
                if len(self.buf) > 300_000:
                    raise RuntimeError('excessive serial output without expected checkpoint')
        raise RuntimeError(f'timeout waiting for {needle!r}')

    def command(self, line: str, expected: str, timeout: float):
        os.write(self.fd, line.encode('ascii') + b'\r')
        self.expect(expected, timeout)
        self.expect('intellect# ', timeout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--boot-timeout', type=float, default=100.0)
    parser.add_argument('--command-timeout', type=float, default=10.0)
    args = parser.parse_args()
    if not (ROOT/'build/intellectos-uefi-stage-e.img').exists():
        print('FAIL: build image absent; run make uefi-check first', file=sys.stderr)
        return 2
    master, slave = pty.openpty()
    child = None
    s = None
    try:
        child = subprocess.Popen(['bash', 'tools/run_uefi_console.sh'], cwd=ROOT,
                                 stdin=slave, stdout=slave, stderr=slave,
                                 start_new_session=True, close_fds=True)
        os.close(slave)
        s = Session(master, child)
        s.expect('KHEAP SLAB/PAGE-RUN: PASS', args.boot_timeout)
        s.expect('M3-A IDENTITY W^X/NX: PASS', args.boot_timeout)
        s.expect('INTELLECTOS C1 / EARLY SERIAL MONITOR', args.boot_timeout)
        s.expect('intellect# ', args.command_timeout)
        s.command('help', 'COMMANDS:', args.command_timeout)
        s.command('mem', 'PMM MANAGED=', args.command_timeout)
        s.command('vmm', 'VMM SCRATCH WINDOW=READY', args.command_timeout)
        s.command('boot', 'BOOTINFO MAGIC=', args.command_timeout)
        s.command('echo M3A_SMOKE_OK', '\nM3A_SMOKE_OK\n', args.command_timeout)
        print('PASS: M3-A QEMU/OVMF boot + NX checkpoint + interactive C1 commands')
        return 0
    except (RuntimeError, OSError) as exc:
        print('FAIL: M3-A QEMU smoke: ' + str(exc), file=sys.stderr)
        if s is not None:
            print('--- QEMU serial output tail ---\n' + s.buf[-7000:], file=sys.stderr)
        return 1
    finally:
        if child is not None and child.poll() is None:
            try:
                os.killpg(child.pid, signal.SIGTERM)
                child.wait(timeout=5)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait(timeout=5)
        try: os.close(master)
        except OSError: pass
        try: os.close(slave)
        except OSError: pass

if __name__ == '__main__':
    raise SystemExit(main())
