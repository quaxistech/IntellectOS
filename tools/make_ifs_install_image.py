#!/usr/bin/env python3
"""Create disk image with FAT32 EFI and real IFS-1 initial sealed system volume.

This is a host image builder, NOT a firmware installer. It does not rewrite
physical media. This profile runs only a build-pinned developer IVX,
not general Ed25519 or a root login policy.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
import re
from pathlib import Path
import sys
import uuid
import struct
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from installer.ifs_disk_v1 import Volume, IFSError
from installer import boot_disk_v1 as disk
from installer.ivx_v1 import verify_dev_terminal
from installer.blake3_ref import blake3
from installer.system_config_v1 import validate_timezone

# Project-specific deterministic GUID, not yet registered with UEFI Forum.
IFS_GPT_GUID=uuid.uuid5(uuid.NAMESPACE_URL,'https://intellectos.org/spec/ifs-1/gpt-type')


def patch_partition_type(path:Path,type_guid:uuid.UUID):
    with path.open('r+b',buffering=0) as f:
        sectors=path.stat().st_size//512
        for hdr_lba,entries_lba in ((1,2),(sectors-1,sectors-33)):
            f.seek(entries_lba*512);table=bytearray(f.read(128*128))
            if len(table)!=128*128 or table[128:144]!=disk.DATA_RESERVED_TYPE.bytes_le:
                raise RuntimeError('unexpected original GPT second partition type')
            table[128:144]=type_guid.bytes_le
            title='IntellectFS v1'.encode('utf-16le')
            table[128+56:128+128]=title.ljust(72,b'\0')
            crc=zlib.crc32(table)&0xffffffff
            f.seek(entries_lba*512);f.write(table)
            f.seek(hdr_lba*512);h=bytearray(f.read(512))
            if h[:8]!=b'EFI PART':raise RuntimeError('bad GPT header')
            struct.pack_into('<I',h,88,crc)
            struct.pack_into('<I',h,16,0)
            struct.pack_into('<I',h,16,zlib.crc32(h[:92])&0xffffffff)
            f.seek(hdr_lba*512);f.write(h)
        f.flush();os.fsync(f.fileno())


def volume_for_image(path:Path):
    n=path.stat().st_size//512
    sys_start=2048+disk.ESP_SIZE//512
    count=n-34-sys_start+1
    return Volume(path,sys_start*512,(count//8)*4096)


def ensure_system(config:bytes,ivx:bytes,public:bytes)->dict[str,bytes]:
    try:record=json.loads(config)
    except Exception as e:raise ValueError('invalid configuration JSON') from e
    if record.get('schema')!='IntellectOS.system_config.v1' or record.get('rtc_policy')!='UTC':
        raise ValueError('bad system config schema')
    validate_timezone(record['timezone_iana'])
    users=record.get('users',[])
    if len(users)!=1 or users[0].get('name')!='root' or users[0].get('uid')!=0:
        raise ValueError('root user missing')
    kdf=users[0].get('password_kdf',{})
    if (kdf.get('kind')!='Argon2id' or kdf.get('version')!=19 or
        not isinstance(kdf.get('memory_kib'),int) or not 65536<=kdf['memory_kib']<=1048576 or
        not isinstance(kdf.get('iterations'),int) or not 2<=kdf['iterations']<=12 or
        kdf.get('parallelism')!=1):
        raise ValueError('root Argon2id verifier/parameters invalid')
    try:
        salt=base64.b64decode(kdf['salt'],validate=True)
        secret_hash=base64.b64decode(kdf['hash'],validate=True)
    except (KeyError,ValueError) as exc:
        raise ValueError('root password verifier invalid') from exc
    if len(salt)!=16 or len(secret_hash)!=32:
        raise ValueError('root password verifier length invalid')
    verify_dev_terminal(ivx,public)
    return {'/sbin/terminal.ivx':ivx,
            '/etc/system.json':config,
            '/etc/timezone':(record['timezone_iana']+'\n').encode(),
            '/etc/trust/developer.pub':public}


def create(path:Path,efi:bytes,ivx:bytes,pub:bytes,config:bytes,size_gib:int)->dict:
    if not 1<=size_gib<=128:raise ValueError('disk image size must be 1..128 GiB')
    files=ensure_system(config,ivx,pub)
    result=disk.build_image(path,efi,size_gib*disk.GiB)
    disk.verify_image(path,efi)
    v=volume_for_image(path)
    try:
        ifs=v.format(files)
        verified=v.check()
        for key,payload in files.items():
            if verified[key].data!=payload:raise RuntimeError('IFS readback differs: '+key)
        patch_partition_type(path,IFS_GPT_GUID)
        old=disk.DATA_RESERVED_TYPE
        try:
            disk.DATA_RESERVED_TYPE=IFS_GPT_GUID
            disk.verify_image(path,efi)
        finally:disk.DATA_RESERVED_TYPE=old
    except BaseException:
        path.unlink(missing_ok=True)
        raise
    return {**result,**ifs,'status':'IFS_INITIAL_SEAL_VALID_PINNED_BOOT_PROFILE',
            'type_guid':str(IFS_GPT_GUID), 'ivx_sha256':hashlib.sha256(ivx).hexdigest()}


def verify_kernel_pin(pin_header:Path,ivx:bytes)->None:
    """Refuse to build a disk image with a different IVX from the kernel pin."""
    text=pin_header.read_text(encoding='ascii')
    m=re.search(r'#define INTELLECT_PINNED_IVX_BYTES (\d+)u',text)
    if not m or int(m.group(1))!=len(ivx):
        raise ValueError('kernel IVX pin length does not match installed file')
    const=re.search(r'intellect_pinned_ivx_blake3\[32\]\s*=\s*\{([^}]*)\}',text)
    if not const:raise ValueError('kernel IVX BLAKE3 pin missing')
    hexes=re.findall(r'0x([0-9a-fA-F]{2})u',const.group(1))
    if len(hexes)!=32 or bytes(int(h,16) for h in hexes)!=blake3(ivx):
        raise ValueError('installed IVX does not match kernel trust pin')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--efi',type=Path,required=True)
    p.add_argument('--ivx',type=Path,required=True)
    p.add_argument('--trusted-public-key',type=Path,required=True)
    p.add_argument('--root-config',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--size-gib',type=int,default=1)
    p.add_argument('--kernel-pin',type=Path,default=ROOT/'build/kernel64/ifs_ivx_pin.h')
    a=p.parse_args()
    ivx=a.ivx.read_bytes()
    verify_kernel_pin(a.kernel_pin,ivx)
    result=create(a.output,a.efi.read_bytes(),ivx,
                  a.trusted_public_key.read_bytes(),a.root_config.read_bytes(),a.size_gib)
    print(json.dumps(result,indent=2))
    print('BOOT POLICY: exact build-pinned developer IVX only; root login is NOT yet implemented')
if __name__=='__main__':main()
