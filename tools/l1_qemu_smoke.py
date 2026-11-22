#!/usr/bin/env python3
"""QEMU serial evidence of reaching L1 screen hook; NOT pixel verification."""
from __future__ import annotations
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
    if not (ROOT/'build/intellectos-uefi-stage-e.img').is_file():
        print('FAIL: missing UEFI image',file=sys.stderr);return 2
    master,slave=pty.openpty();child=None
    buf=''
    try:
        child=subprocess.Popen(['bash','tools/run_uefi_console.sh'],cwd=ROOT,
            stdin=slave,stdout=slave,stderr=slave,
            close_fds=True,start_new_session=True)
        os.close(slave);slave=-1
        deadline=time.monotonic()+110.0
        markers=['M3-A IDENTITY W^X/NX: PASS',
                 'H1/H2 DRIVER REGISTRY: READY',
                 'L1 WELCOME FRAMEBUFFER: READY',
                 'intellect# ']
        idx=0
        sent_exit=False
        while time.monotonic()<deadline and idx<len(markers):
            if not sent_exit and 'intellect:/$ ' in buf:
                os.write(master, b'exit\r')
                sent_exit=True
            if child.poll() is not None:raise RuntimeError('QEMU exited before readiness')
            ready,_,_=select.select([master],[],[],0.25)
            if not ready:continue
            try:chunk=os.read(master,8192)
            except OSError as e:
                if e.errno==errno.EIO:raise RuntimeError('QEMU PTY closed') from e
                raise
            if not chunk:raise RuntimeError('QEMU serial closed')
            buf+=chunk.decode('utf-8','replace').replace('\r\n','\n').replace('\r','\n')
            if len(buf)>500000:raise RuntimeError('serial transcript exceeded cap')
            while idx<len(markers):
                pos=buf.find(markers[idx])
                if pos<0:break
                buf=buf[pos+len(markers[idx]):]
                idx+=1
        if idx!=len(markers):raise RuntimeError('timeout waiting for '+markers[idx])
        os.write(master,b'echo LIVE_USB_L1_OK\r')
        deadline=time.monotonic()+12.0
        while time.monotonic()<deadline and '\nLIVE_USB_L1_OK\n' not in buf:
            ready,_,_=select.select([master],[],[],0.25)
            if ready:
                chunk=os.read(master,8192)
                if not chunk:break
                buf+=chunk.decode('utf-8','replace').replace('\r\n','\n').replace('\r','\n')
        if '\nLIVE_USB_L1_OK\n' not in buf:
            raise RuntimeError('console echo did not reply')
        print('PASS: L1 QEMU boots native kernel, runs H1/H2, renders splash hook and accepts console')
        print('NOTE: framebuffer pixels still require GUI QEMU or physical display inspection')
        return 0
    except (OSError,RuntimeError) as e:
        print('FAIL: L1 QEMU smoke: '+str(e),file=sys.stderr)
        print('--- serial tail ---\n'+buf[-10000:],file=sys.stderr)
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
