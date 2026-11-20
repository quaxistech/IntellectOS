#!/usr/bin/env python3
"""Host-only root password record; never installs it to an ESP or kernel.
"""
import argparse
import getpass
import os
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from installer.system_config_v1 import provision,canonical_json,ConfigError

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--timezone',default='UTC')
    ap.add_argument('--output',type=Path,required=True,
                    help='write new chmod-0600 file on host for future IFS install')
    opt=ap.parse_args()
    if opt.output.exists():raise SystemExit('BLOCKED: config already exists')
    first=getpass.getpass('root password (12+ UTF-8 bytes): ')
    again=getpass.getpass('confirm root password: ')
    if first!=again:raise SystemExit('BLOCKED: password mismatch')
    try: blob=canonical_json(provision(first,opt.timezone))
    except ConfigError as e:raise SystemExit('BLOCKED: '+str(e)) from e
    first=again=None
    fd=os.open(opt.output,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'wb') as f:
        f.write(blob);f.flush();os.fsync(f.fileno())
    print('PASS: private root config created; NOT installed to IntellectFS')
if __name__=='__main__': main()
