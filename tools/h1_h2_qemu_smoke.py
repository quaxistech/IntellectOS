#!/usr/bin/env python3
"""End-to-end read-only UEFI/QEMU H1/H2 console test, no physical media."""
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

ROOT=Path(__file__).resolve().parents[1]
class Session:
    def __init__(self,fd,child):
        self.fd=fd;self.child=child;self.buf='';self.offset=0
    def expect(self,needle,timeout):
        stop=time.monotonic()+timeout
        while time.monotonic()<stop:
            pos=self.buf.find(needle,self.offset)
            if pos>=0:
                self.offset=pos+len(needle)
                return
            if self.child.poll() is not None:
                raise RuntimeError('QEMU exited while awaiting '+repr(needle))
            ready,_,_=select.select([self.fd],[],[],min(.3,max(0.,stop-time.monotonic())))
            if ready:
                try:chunk=os.read(self.fd,65536)
                except OSError as e:
                    if e.errno==errno.EIO:raise RuntimeError('QEMU PTY closed') from e
                    raise
                if not chunk:raise RuntimeError('PTY EOF')
                self.buf+=chunk.decode('utf-8',errors='replace').replace('\r\n','\n').replace('\r','\n')
                if len(self.buf)>500000:raise RuntimeError('unbounded guest output')
        raise RuntimeError('timeout awaiting '+repr(needle))
    def command(self,cmd,needle,timeout):
        os.write(self.fd,cmd.encode('ascii')+b'\r')
        self.expect(needle,timeout)
        self.expect('intellect# ',timeout)

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--boot-timeout',type=float,default=100.)
    p.add_argument('--command-timeout',type=float,default=10.)
    args=p.parse_args()
    if not (ROOT/'build/intellectos-uefi-stage-e.img').is_file():
        print('FAIL: no QEMU image, run make clean check',file=sys.stderr);return 2
    master,slave=pty.openpty();child=None;session=None
    try:
        child=subprocess.Popen(['bash','tools/run_uefi_console.sh'],cwd=ROOT,
            stdin=slave,stdout=slave,stderr=slave,start_new_session=True,close_fds=True)
        os.close(slave);slave=-1
        session=Session(master,child)
        session.expect('M3-A IDENTITY W^X/NX: PASS',args.boot_timeout)
        session.expect('H1/H2 HARDWARE INVENTORY: READY',args.boot_timeout)
        session.expect('H1/H2 DRIVER REGISTRY: READY',args.boot_timeout)
        session.expect('intellect# ',args.command_timeout)
        session.command('hw summary','H1 INVENTORY: CPU=',args.command_timeout)
        session.command('hw drivers','H2 BUILTIN DRIVER REGISTRY',args.command_timeout)
        session.command('hw pci','PCI CONF1=READY',args.command_timeout)
        session.command('hw acpi','ACPI RSDP=',args.command_timeout)
        session.command('hw cpu','CPU VENDOR=',args.command_timeout)
        session.command('echo H1H2_SMOKE_OK','H1H2_SMOKE_OK\n',args.command_timeout)
        print('PASS: IntellectOS H1/H2 boot + PCI/ACPI inventory + built-in driver registry + C1 commands')
        return 0
    except (OSError,RuntimeError) as e:
        print('FAIL: H1/H2 QEMU smoke: '+str(e),file=sys.stderr)
        if session:print('--- guest serial tail ---\n'+session.buf[-7000:],file=sys.stderr)
        return 1
    finally:
        if child and child.poll() is None:
            try:
                os.killpg(child.pid,signal.SIGTERM);child.wait(timeout=5)
            except (ProcessLookupError,subprocess.TimeoutExpired):
                try:os.killpg(child.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                child.wait(timeout=5)
        os.close(master)
        if slave>=0:os.close(slave)
if __name__=='__main__':raise SystemExit(main())
