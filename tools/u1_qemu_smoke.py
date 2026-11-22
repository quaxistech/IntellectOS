#!/usr/bin/env python3
"""QEMU U1 real CPL3 execution. Serial responses are evidence, not simulated.
B1 UEFI edition menu defaults to Terminal after 12 seconds.
"""
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

def main():
    master,slave=pty.openpty(); child=None; buf=''
    try:
        child=subprocess.Popen(['bash','tools/run_uefi_console.sh'],cwd=ROOT,
                               stdin=slave,stdout=slave,stderr=slave,
                               close_fds=True,start_new_session=True)
        os.close(slave);slave=-1
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            if child.poll() is not None:raise RuntimeError('QEMU exited')
            ready,_,_=select.select([master],[],[],0.3)
            if ready:
                try:chunk=os.read(master,8192)
                except OSError as e:
                    if e.errno==errno.EIO:raise RuntimeError('PTY closed') from e
                    raise
                if not chunk:raise RuntimeError('serial channel closed')
                buf+=chunk.decode('utf8','replace').replace('\r\n','\n').replace('\r','\n')
                if len(buf)>500000:raise RuntimeError('output cap exceeded')
                if 'intellect:/$ ' in buf and 'U1: Ring3 user address space installed' in buf:
                    break
        else:raise RuntimeError('timeout: U1 user terminal not reached')
        os.write(master,b'help\r')
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            ready,_,_=select.select([master],[],[],0.25)
            if ready:
                chunk=os.read(master,8192)
                buf+=chunk.decode('utf8','replace').replace('\r\n','\n').replace('\r','\n')
            if 'COMMANDS: help about pwd echo <text> mem clear' in buf and 'exit' in buf:break
        else:raise RuntimeError('no user-mode help reply')
        os.write(master,b'echo U1_RING3_OK\r')
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            ready,_,_=select.select([master],[],[],0.25)
            if ready:
                chunk=os.read(master,8192)
                buf+=chunk.decode('utf8','replace').replace('\r\n','\n').replace('\r','\n')
            if '\nU1_RING3_OK\n' in buf:break
        else:raise RuntimeError('no user-mode echo reply')
        os.write(master,b'exit\r')
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            ready,_,_=select.select([master],[],[],0.25)
            if ready:
                chunk=os.read(master,8192)
                buf+=chunk.decode('utf8','replace').replace('\r\n','\n').replace('\r','\n')
            if 'U1: user terminal exited' in buf:break
        else:raise RuntimeError('no user-mode exit confirmation')
        print('PASS: U1 QEMU boot -> user CPL3 terminal -> int80 read/write -> exit to kernel')
        return 0
    except (OSError,RuntimeError) as e:
        print('FAIL: U1 smoke: '+str(e),file=sys.stderr)
        print('--- QEMU serial tail ---\n'+buf[-12000:],file=sys.stderr)
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
