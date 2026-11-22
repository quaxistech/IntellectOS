#!/usr/bin/env python3
"""QEMU/OVMF read-only ATA smoke: only an IDE image file, never a host disk."""
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

class Guest:
    def __init__(self,fd,child):
        self.fd=fd;self.child=child;self.buffer='';self.cursor=0
    def expect(self,text,timeout):
        deadline=time.monotonic()+timeout
        sent_exit=False
        while time.monotonic()<deadline:
            found=self.buffer.find(text,self.cursor)
            if found>=0:
                self.cursor=found+len(text);return
            if not sent_exit and 'intellect:/$ ' in self.buffer and text != 'intellect:/$ ':
                os.write(self.fd, b'exit\r')
                sent_exit = True
            if self.child.poll() is not None:
                raise RuntimeError('QEMU terminated awaiting '+repr(text))
            timeout_left=max(0,deadline-time.monotonic())
            ready,_,_=select.select([self.fd],[],[],min(0.25,timeout_left))
            if not ready:continue
            try:data=os.read(self.fd,32768)
            except OSError as e:
                if e.errno==errno.EIO:raise RuntimeError('serial PTY closed') from e
                raise
            if not data:raise RuntimeError('serial PTY EOF')
            self.buffer+=data.decode('utf-8','replace').replace('\r\n','\n').replace('\r','\n')
            if len(self.buffer)>500000:raise RuntimeError('unbounded serial output')
        raise RuntimeError('timed out waiting for '+repr(text))
    def command(self,command,response,timeout):
        os.write(self.fd,command.encode('ascii')+b'\r')
        self.expect(response,timeout)
        self.expect('intellect# ',timeout)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--boot-timeout',type=float,default=110.)
    ap.add_argument('--command-timeout',type=float,default=12.)
    args=ap.parse_args()
    image=ROOT/'build/intellectos-uefi-stage-e.img'
    if not image.is_file():
        print('FAIL: missing QEMU disk image; run make clean check',file=sys.stderr)
        return 2
    master,slave=pty.openpty()
    child=None;guest=None
    try:
        child=subprocess.Popen(['bash','tools/run_uefi_console.sh'],cwd=ROOT,
             stdin=slave,stdout=slave,stderr=slave,
             start_new_session=True,close_fds=True)
        os.close(slave);slave=-1
        guest=Guest(master,child)
        guest.expect('M3-A IDENTITY W^X/NX: PASS',args.boot_timeout)
        guest.expect('H1/H2 DRIVER REGISTRY: READY',args.boot_timeout)
        guest.expect('S0 BLOCK READONLY: READY',args.boot_timeout)
        guest.expect('intellect# ',args.command_timeout)
        guest.command('disk info','S0 DISK: READY MODEL=',args.command_timeout)
        guest.command('disk mbr','S0 MBR: SIGNATURE=55AA',args.command_timeout)
        guest.command('disk boot','S0 PBR: LBA=',args.command_timeout)
        guest.command('disk gpt','S0 GPT: NOT_PRESENT (MBR disk)',args.command_timeout)
        guest.command('hw drivers','DRIVER piix-ata-pio-readonly STATE=ACTIVE ATTACHED=1',args.command_timeout)
        guest.command('echo S0_SMOKE_OK','\nS0_SMOKE_OK\n',args.command_timeout)
        print('PASS: QEMU/OVMF native IntellectOS ATA PIO read-only disk + MBR + PBR + driver registry')
        return 0
    except (OSError,RuntimeError) as err:
        print('FAIL: S0 QEMU smoke: '+str(err),file=sys.stderr)
        if guest:print('--- guest serial tail ---\n'+guest.buffer[-12000:],file=sys.stderr)
        return 1
    finally:
        if child and child.poll() is None:
            try:os.killpg(child.pid,signal.SIGTERM);child.wait(timeout=5)
            except (ProcessLookupError,subprocess.TimeoutExpired):
                try:os.killpg(child.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                child.wait(timeout=5)
        os.close(master)
        if slave>=0:os.close(slave)
if __name__=='__main__':raise SystemExit(main())
