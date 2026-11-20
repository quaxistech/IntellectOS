"""GPT + FAT32 ESP image writer and *independent* on-image verifier.
Host-only. Never accepts /dev/* or block special files. 512-byte sectors.
This makes a bootable UEFI image containing the existing EFI program, but
DOES NOT format IntellectFS or produce a completed installed root volume.
"""
from __future__ import annotations
import hashlib
import os
from pathlib import Path
import stat
import struct
import uuid
import zlib

SECTOR=512
MiB=1024*1024
GiB=1024*MiB
ESP_SIZE=512*MiB
ALIGN=2048
ESP_TYPE=uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b')
# Deliberate generic placeholder type, NOT a registered IntellectFS type GUID.
DATA_RESERVED_TYPE=uuid.UUID('0fc63daf-8483-4772-8e79-3d69d8477de4')
HEADER_SIGNATURE=b'EFI PART'

class DiskFormatError(ValueError):
    pass


def guard_regular_target(path: Path) -> None:
    if path.exists():
        meta=path.lstat()
        if not stat.S_ISREG(meta.st_mode) or meta.st_nlink != 1:
            raise DiskFormatError('output must be a normal unshared file')
        if meta.st_size != 0:
            raise DiskFormatError('output exists and is nonempty; refuse overwrite')
    if str(path).startswith('/dev/'):
        raise DiskFormatError('direct block-device targets forbidden')


def part_entry(kind: uuid.UUID, partid: uuid.UUID, begin: int,
               end: int, name: str) -> bytes:
    if begin > end:
        raise DiskFormatError('partition range inverted')
    encoded=name.encode('utf-16le')
    if len(encoded)>72:
        raise DiskFormatError('partition name too long')
    return kind.bytes_le+partid.bytes_le+struct.pack('<QQQ',begin,end,0)+encoded.ljust(72,b'\0')


def gpt_header(current: int, backup: int, first: int, last: int, diskid: uuid.UUID,
               entries_lba: int, entries_crc: int) -> bytes:
    h=bytearray(SECTOR)
    struct.pack_into('<8sIIIIQQQQ16sQIII',h,0,
        HEADER_SIGNATURE,0x00010000,92,0,0,current,backup,first,last,
        diskid.bytes_le,entries_lba,128,128,entries_crc)
    crc=zlib.crc32(h[:92])&0xffffffff
    struct.pack_into('<I',h,16,crc)
    return bytes(h)


def protective_mbr(sectors: int) -> bytes:
    b=bytearray(SECTOR)
    # Protective entry; never mark active or add hybrid entries.
    b[446+1:446+4]=b'\x00\x02\x00'  # Start CHS 0/1/1
    b[446+4]=0xee
    b[446+5:446+8]=b'\xff\xff\xff'  # Saturated end CHS
    struct.pack_into('<II',b,446+8,1,min(sectors-1,0xffffffff))
    b[510:512]=b'\x55\xaa'
    return bytes(b)


def write_at(file, offset: int, data: bytes) -> None:
    file.seek(offset)
    file.write(data)


def short_entry(name: bytes, first_cluster: int, size: int, folder: bool=False) -> bytes:
    if len(name)!=11:
        raise DiskFormatError('8.3 name must be 11 bytes')
    e=bytearray(32)
    e[:11]=name
    e[11]=0x10 if folder else 0x20
    struct.pack_into('<H',e,20,(first_cluster >> 16)&0xffff)
    struct.pack_into('<H',e,26,first_cluster &0xffff)
    struct.pack_into('<I',e,28,size)
    return bytes(e)


def fs_cluster_offset(esp_lba: int, fatsz: int, cluster: int) -> int:
    if cluster<2:
        raise DiskFormatError('invalid FAT32 cluster')
    return (esp_lba+32+2*fatsz+(cluster-2)*8)*SECTOR


def write_fat32_esp(file, esp_lba: int, esp_count: int,
                    efi: bytes, public_files: dict[str,bytes]|None=None) -> None:
    if esp_count*SECTOR != ESP_SIZE:
        raise DiskFormatError('FAT32 ESP size must be 512 MiB')
    if not efi.startswith(b'MZ') or len(efi)>16*MiB:
        raise DiskFormatError('EFI must be a PE/COFF MZ file <=16MiB')
    public_files = dict(public_files or {})
    if len(public_files)>1 or (public_files and set(public_files)!={'README.TXT'}):
        raise DiskFormatError('only optional README.TXT supported')
    fatsz=1
    while True:
        clusters=(esp_count-32-2*fatsz)//8
        need=((clusters+2)*4+SECTOR-1)//SECTOR
        if need==fatsz:
            break
        fatsz=need
    if not 65525<=clusters<0x0ffffff5:
        raise DiskFormatError('not a FAT32 cluster count')
    # Cluster map: 2 root; 3 EFI; 4 BOOT; optional 5 INTELLECT, 6 README.
    cursor=5
    extra_dir=extra_file=0
    if public_files:
        extra_dir=cursor;cursor+=1
        extra_file=cursor;cursor+=1
    efi_start=cursor
    file_clusters=(len(efi)+4095)//4096
    if cursor+file_clusters>=clusters+2:
        raise DiskFormatError('EFI does not fit')
    fat=bytearray(fatsz*SECTOR)
    def set_entry(cl: int,val: int) -> None:
        struct.pack_into('<I',fat,4*cl,val)
    set_entry(0,0x0ffffff8);set_entry(1,0x0fffffff)
    for c in [2,3,4]+([extra_dir,extra_file] if public_files else []):set_entry(c,0x0fffffff)
    for i in range(file_clusters):
        set_entry(efi_start+i,0x0fffffff if i+1==file_clusters else efi_start+i+1)
    free=clusters-(cursor-2)-file_clusters
    if free<0:raise DiskFormatError('FAT overallocated')
    boot=bytearray(SECTOR)
    boot[:3]=b'\xeb\x58\x90';boot[3:11]=b'INTLECT '
    struct.pack_into('<H',boot,11,512)
    boot[13]=8 # 4KiB clusters
    struct.pack_into('<H',boot,14,32)
    boot[16]=2
    struct.pack_into('<H',boot,17,0)
    struct.pack_into('<H',boot,19,0)
    boot[21]=0xf8
    struct.pack_into('<H',boot,22,0)
    struct.pack_into('<H',boot,24,63)
    struct.pack_into('<H',boot,26,255)
    struct.pack_into('<II',boot,28,esp_lba,esp_count)
    struct.pack_into('<I',boot,36,fatsz)
    struct.pack_into('<HHIHH',boot,40,0,0,2,1,6)
    boot[64]=0x80;boot[66]=0x29
    struct.pack_into('<I',boot,67,zlib.crc32(efi)&0xffffffff)
    boot[71:82]=b'INTELLECTOS'
    boot[82:90]=b'FAT32   '
    boot[510:512]=b'\x55\xaa'
    info=bytearray(SECTOR)
    struct.pack_into('<I',info,0,0x41615252)
    struct.pack_into('<I',info,484,0x61417272)
    struct.pack_into('<II',info,488,free,cursor+file_clusters)
    struct.pack_into('<I',info,508,0xaa550000)
    write_at(file,esp_lba*SECTOR,boot)
    write_at(file,(esp_lba+1)*SECTOR,info)
    write_at(file,(esp_lba+6)*SECTOR,boot)
    write_at(file,(esp_lba+7)*SECTOR,info)
    write_at(file,(esp_lba+32)*SECTOR,fat)
    write_at(file,(esp_lba+32+fatsz)*SECTOR,fat)
    root=bytearray(4096)
    root[:32]=short_entry(b'EFI        ',3,0,True)
    if public_files:root[32:64]=short_entry(b'INTLLECT   ',extra_dir,0,True)
    write_at(file,fs_cluster_offset(esp_lba,fatsz,2),root)
    efi_dir=bytearray(4096)
    efi_dir[:32]=short_entry(b'.          ',3,0,True)
    efi_dir[32:64]=short_entry(b'..         ',2,0,True)
    efi_dir[64:96]=short_entry(b'BOOT       ',4,0,True)
    write_at(file,fs_cluster_offset(esp_lba,fatsz,3),efi_dir)
    boot_dir=bytearray(4096)
    boot_dir[:32]=short_entry(b'.          ',4,0,True)
    boot_dir[32:64]=short_entry(b'..         ',3,0,True)
    boot_dir[64:96]=short_entry(b'BOOTX64 EFI',efi_start,len(efi))
    write_at(file,fs_cluster_offset(esp_lba,fatsz,4),boot_dir)
    if public_files:
        data=public_files['README.TXT']
        if len(data)>4096:
            raise DiskFormatError('README too large')
        directory=bytearray(4096)
        directory[:32]=short_entry(b'.          ',extra_dir,0,True)
        directory[32:64]=short_entry(b'..         ',2,0,True)
        directory[64:96]=short_entry(b'README  TXT',extra_file,len(data))
        write_at(file,fs_cluster_offset(esp_lba,fatsz,extra_dir),directory)
        write_at(file,fs_cluster_offset(esp_lba,fatsz,extra_file),data)
    for i in range(file_clusters):
        write_at(file,fs_cluster_offset(esp_lba,fatsz,efi_start+i),efi[i*4096:(i+1)*4096])


def build_image(dest: Path, efi: bytes, size: int, *, guid: uuid.UUID|None=None) -> dict:
    """Create sparse bootable installer-development image (not complete OS)."""
    if size<1024*MiB or size%SECTOR:
        raise DiskFormatError('minimum size 1GiB and multiple of 512')
    if size>128*GiB:
        raise DiskFormatError('image size exceeds bound')
    guard_regular_target(dest)
    sectors=size//SECTOR
    first,last=34,sectors-34
    esp_start=2048
    esp_count=ESP_SIZE//SECTOR
    system_start=esp_start+esp_count
    if (system_start%ALIGN)!=0 or last-system_start+1<256*MiB//SECTOR:
        raise DiskFormatError('system partition below 256MiB')
    disk_guid=guid or uuid.uuid4()
    entries=bytearray(128*128)
    entries[:128]=part_entry(ESP_TYPE,uuid.uuid4(),esp_start,system_start-1,'EFI System')
    entries[128:256]=part_entry(DATA_RESERVED_TYPE,uuid.uuid4(),system_start,last,'IFS RESERVED (UNFORMATTED)')
    crc=zlib.crc32(entries)&0xffffffff
    hdr1=gpt_header(1,sectors-1,first,last,disk_guid,2,crc)
    hdr2=gpt_header(sectors-1,1,first,last,disk_guid,sectors-33,crc)
    dest.parent.mkdir(parents=True,exist_ok=True)
    try:
        with dest.open('xb') as f:
            f.truncate(size)
            write_at(f,0,protective_mbr(sectors))
            write_at(f,SECTOR,hdr1)
            write_at(f,2*SECTOR,entries)
            write_at(f,(sectors-33)*SECTOR,entries)
            write_at(f,(sectors-1)*SECTOR,hdr2)
            notice=b'INTELLECTOS INSTALL DEVELOPMENT IMAGE\r\nSYSTEM PARTITION NOT FORMATTED\r\nNO ROOT LOGIN OR IFS MOUNT IMPLEMENTED\r\n'
            write_fat32_esp(f,esp_start,esp_count,efi,{'README.TXT':notice})
            f.flush()
            os.fsync(f.fileno())
    except BaseException:
        dest.unlink(missing_ok=True)
        raise
    return {'image':str(dest),'size_bytes':size,'disk_guid':str(disk_guid),
            'esp_start_lba':esp_start,'esp_sectors':esp_count,
            'ifs_reserved_start_lba':system_start,'ifs_reserved_last_lba':last,
            'efi_sha256':hashlib.sha256(efi).hexdigest(),
            'status':'PREINSTALL_ONLY_NO_IFS_NO_LOGIN'}


def read_exact(f, off: int, n: int) -> bytes:
    f.seek(off)
    d=f.read(n)
    if len(d)!=n:raise DiskFormatError('truncated on-disk data')
    return d


def verify_image(path: Path, expected_efi: bytes|None=None) -> dict:
    if not path.is_file():raise DiskFormatError('not a regular image')
    size=path.stat().st_size
    if size<1024*MiB or size%SECTOR:raise DiskFormatError('invalid size')
    n=size//SECTOR
    with path.open('rb') as f:
        mbr=read_exact(f,0,SECTOR)
        if mbr[510:512]!=b'\x55\xaa' or mbr[450]!=0xee or mbr[446]!=0:
            raise DiskFormatError('protective MBR invalid')
        def check_header(lba: int,peer: int,entries_lba: int):
            h=bytearray(read_exact(f,lba*SECTOR,SECTOR))
            if h[:8]!=b'EFI PART' or struct.unpack_from('<III',h,8)!=(0x10000,92,struct.unpack_from('<I',h,16)[0]):
                # explicit header checks below; this preliminary check also rejects bad revision/length.
                raise DiskFormatError('bad GPT header revision/size')
            stored=struct.unpack_from('<I',h,16)[0]
            h[16:20]=b'\0'*4
            if zlib.crc32(h[:92])&0xffffffff!=stored:
                raise DiskFormatError('GPT header CRC mismatch')
            current,backup,first,last=struct.unpack_from('<QQQQ',h,24)
            if (current,backup,first,last)!=(lba,peer,34,n-34):
                raise DiskFormatError('GPT geometry mismatch')
            if struct.unpack_from('<QII',h,72)!=(entries_lba,128,128):
                raise DiskFormatError('GPT entry table geometry mismatch')
            entries=read_exact(f,entries_lba*SECTOR,128*128)
            if zlib.crc32(entries)&0xffffffff!=struct.unpack_from('<I',h,88)[0]:
                raise DiskFormatError('GPT entry CRC mismatch')
            return bytes(h),entries
        ph,pe=check_header(1,n-1,2)
        bh,be=check_header(n-1,1,n-33)
        if pe!=be or ph[56:72]!=bh[56:72]:raise DiskFormatError('GPT mirrors disagree')
        a,b=pe[:128],pe[128:256]
        if uuid.UUID(bytes_le=a[:16])!=ESP_TYPE or uuid.UUID(bytes_le=b[:16])!=DATA_RESERVED_TYPE:
            raise DiskFormatError('GPT partition type mismatch')
        esp_start,esp_last=struct.unpack_from('<QQ',a,32)
        sys_start,sys_last=struct.unpack_from('<QQ',b,32)
        if (esp_start,esp_last,sys_start,sys_last)!=(2048,2048+ESP_SIZE//SECTOR-1,
                                                    2048+ESP_SIZE//SECTOR,n-34):
            raise DiskFormatError('GPT layout mismatch')
        if any(pe[256:]):raise DiskFormatError('unexpected extra partitions')
        boot=read_exact(f,esp_start*SECTOR,SECTOR)
        if boot[510:512]!=b'\x55\xaa' or boot[82:90]!=b'FAT32   ':
            raise DiskFormatError('invalid FAT32 boot sector')
        spc=boot[13]
        fatsz=struct.unpack_from('<I',boot,36)[0]
        if (struct.unpack_from('<H',boot,11)[0],spc,struct.unpack_from('<H',boot,14)[0],boot[16])!=(512,8,32,2):
            raise DiskFormatError('invalid FAT32 geometry')
        if read_exact(f,(esp_start+6)*SECTOR,SECTOR)!=boot:
            raise DiskFormatError('FAT backup boot sector mismatch')
        info=read_exact(f,(esp_start+1)*SECTOR,SECTOR)
        if info!=read_exact(f,(esp_start+7)*SECTOR,SECTOR) or struct.unpack_from('<I',info,0)[0]!=0x41615252:
            raise DiskFormatError('FSInfo mismatch')
        fat1=read_exact(f,(esp_start+32)*SECTOR,fatsz*SECTOR)
        fat2=read_exact(f,(esp_start+32+fatsz)*SECTOR,fatsz*SECTOR)
        if fat1!=fat2:raise DiskFormatError('FAT mirrors disagree')
        def cl(off):return read_exact(f,fs_cluster_offset(esp_start,fatsz,off),4096)
        def entry(folder: bytes,name: bytes):
            for off in range(0,4096,32):
                e=folder[off:off+32]
                if e[0]==0:break
                if e[:11]==name:return e
            raise DiskFormatError('missing path segment '+repr(name))
        root=cl(2)
        e=entry(root,b'EFI        ')
        if e[11]!=0x10:raise DiskFormatError('EFI not a directory')
        efi_dir=cl(struct.unpack_from('<H',e,26)[0]);b=entry(efi_dir,b'BOOT       ')
        boot_dir=cl(struct.unpack_from('<H',b,26)[0]);ex=entry(boot_dir,b'BOOTX64 EFI')
        count=struct.unpack_from('<I',ex,28)[0]
        cluster=struct.unpack_from('<H',ex,26)[0]
        if count<2 or count>16*MiB:raise DiskFormatError('bad EFI size')
        chain=set();payload=bytearray()
        while True:
            if cluster<2 or cluster>=len(fat1)//4 or cluster in chain:
                raise DiskFormatError('bad FAT cluster chain')
            chain.add(cluster)
            payload.extend(cl(cluster))
            if len(payload)>count+4096:raise DiskFormatError('FAT chain exceeds file length')
            nxt=struct.unpack_from('<I',fat1,cluster*4)[0]&0x0fffffff
            if nxt>=0x0ffffff8:break
            cluster=nxt
        efi=bytes(payload[:count])
        if not efi.startswith(b'MZ') or (expected_efi is not None and efi!=expected_efi):
            raise DiskFormatError('EFI file differs from expected bytes')
        return {'size_bytes':size,'gpt':'PRIMARY_AND_BACKUP_VALID',
                'esp':'FAT32_PATH_VALID','ifs':'RESERVED_NOT_FORMATTED',
                'efi_sha256':hashlib.sha256(efi).hexdigest(),'efi_size':len(efi),
                'boot_file_cluster_count':len(chain)}
