#!/usr/bin/env python3
"""Real OVMF/QEMU gate: GPT HDD + IntellectFS verified root + Ring3 terminal.

This does not emulate firmware output. No physical disk is accessed. The
EFI must have been built from the checked-out IntellectOS source code.
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

ROOT=Path(__file__).resolve().parents[1]

def firmware():
    for code,vars in (('/usr/share/OVMF/OVMF_CODE_4M.fd','/usr/share/OVMF/OVMF_VARS_4M.fd'),
                      ('/usr/share/OVMF/OVMF_CODE.fd','/usr/share/OVMF/OVMF_VARS.fd')):
        if Path(code).is_file() and Path(vars).is_file():return code,vars
    raise RuntimeError('OVMF firmware unavailable (sudo apt install ovmf)')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('image',type=Path)
    p.add_argument('--timeout',type=int,default=140)
    a=p.parse_args()
    if not a.image.is_file():raise SystemExit('BLOCKED: image must be a regular file')
    code,vars=firmware()
    store=ROOT/'build'/'hdd-qemu';store.mkdir(parents=True,exist_ok=True)
    varcopy=store/'OVMF_VARS_HDD.fd';varcopy.write_bytes(Path(vars).read_bytes())
    cmd=['qemu-system-x86_64','-m','256M','-net','none','-monitor','none',
         '-no-reboot','-display','none','-serial','stdio',
         '-drive',f'if=pflash,format=raw,readonly=on,file={code}',
         '-drive',f'if=pflash,format=raw,file={varcopy}',
         '-drive',f'if=ide,format=raw,file={a.image.resolve()}']
    master,slave=pty.openpty();child=None;output=''
    try:
        child=subprocess.Popen(cmd,cwd=ROOT,stdin=slave,stdout=slave,stderr=slave,
                               close_fds=True,start_new_session=True)
        os.close(slave);slave=-1
        def until(expected:str,deadline:float):
            nonlocal output
            while time.monotonic()<deadline:
                if child.poll() is not None:raise RuntimeError(f'QEMU terminated ({child.returncode})')
                rd,_,_=select.select([master],[],[],0.25)
                if rd:
                    try:data=os.read(master,32768)
                    except OSError as e:
                        if e.errno==errno.EIO:raise RuntimeError('serial PTY closed') from e
                        raise
                    if not data:raise RuntimeError('serial channel closed')
                    output+=data.decode('utf-8','replace').replace('\r\n','\n').replace('\r','\n')
                    if len(output)>800000:raise RuntimeError('output limit')
                    if expected in output:return
            raise RuntimeError('timeout waiting for '+repr(expected))
        until('IFS-1: SEALED SYSTEM ROOT VERIFIED',time.monotonic()+a.timeout)
        until('IFS-1: VERIFIED PINNED IVX FROM DISK',time.monotonic()+20)
        until('IFS-1: timezone ',time.monotonic()+20)
        until('U1: Ring3 user address space installed',time.monotonic()+20)
        until('intellect:/$ ',time.monotonic()+15)
        os.write(master,b'help\r')
        until('COMMANDS: help about pwd echo <text> mem clear',time.monotonic()+20)
        os.write(master,b'echo U1_HDD_BOOT_VERIFIED\r')
        until('\nU1_HDD_BOOT_VERIFIED\n',time.monotonic()+20)
        os.write(master,b'exit\r')
        until('U1: user terminal exited',time.monotonic()+20)
        print('PASS: GPT HDD -> native IFS-1 verified root -> pinned IVX from disk -> Ring3 terminal')
        return 0
    except (RuntimeError,OSError) as e:
        print('FAIL: '+str(e),file=sys.stderr)
        print('---- Real QEMU serial output tail ----\n'+output[-12000:],file=sys.stderr)
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
