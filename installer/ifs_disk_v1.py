"""IntellectFS IFS-1, deterministic single-shard initial-volume encoder/fsck.

This is a bounded BOOT PROFILE reference implementation, NOT a general RW
filesystem: initial tree + data extents + commit descriptor + durable seal.
It enforces the normative IFSBootstrap/Seal/Node/Pointer wire layouts.
Only regular IMAGE files are writable; physical devices and links are refused.
The resulting volume is readable by an independent IFS-1 parser if the boot
profile in docs/IntellectFS_v1_BOOT_PROFILE_WIRE.md is implemented.
"""
from __future__ import annotations
import io
import os
import secrets
from pathlib import Path
import stat
import struct
import uuid
from dataclasses import dataclass
from typing import BinaryIO

from .blake3_ref import blake3

BLOCK=4096
MIN_BLOCKS=65536
BOOT_MAGIC=b'IFSB0001'
SEAL_MAGIC=b'IFSS0001'
NODE_MAGIC=b'IFSN0001'
DESC_MAGIC=b'IFSC0001'
LAYOUT_MAGIC=b'IFSL0001'
BOOT_DOMAIN=b'IFS/bootstrap/v1\0'
SEAL_DOMAIN=b'IFS/seal/v1\0'
NODE_DOMAIN=b'IFS/node/v1\0'
PTR_DOMAIN=b'IFS/pointer/v1\0'
BODY_DOMAIN=b'IFS/commit-body/v1\0'
DESC_DOMAIN=b'IFS/commit-desc/v1\0'
ROOT_DOMAIN=b'IFS/root-summary/v1\0'
LAYOUT_DOMAIN=b'IFS/layout/v1\0'

# Fixed boot profile; all coordinates are partition-relative pblk values.
ANCHOR_START=3
ANCHOR_SLOTS=16
RECOVERY_START=19
RECOVERY_BLOCKS=64
LAYOUT_PBLK=19
DESCRIPTOR_PBLK=20
ROOT_FIRST=256
DATA_MIN=16384

class IFSError(ValueError):
    pass


def need(check:bool, text:str)->None:
    if not check: raise IFSError(text)


def u16(b:bytes,off:int)->int:return struct.unpack_from('<H',b,off)[0]
def u32(b:bytes,off:int)->int:return struct.unpack_from('<I',b,off)[0]
def u64(b:bytes,off:int)->int:return struct.unpack_from('<Q',b,off)[0]

def crc32c(data:bytes)->int:
    crc=0xffffffff
    for octet in data:
        crc^=octet
        for _ in range(8):crc=(crc>>1) ^ (0x82f63b78 if (crc&1) else 0)
    return crc^0xffffffff


def digest_masked(domain:bytes,blob:bytes,offset:int,size:int)->bytes:
    b=bytearray(blob);b[offset:offset+size]=bytes(size)
    return blake3(domain+bytes(b))


def make_bootstrap(uid:bytes,volume_blocks:int,sector_bytes:int,gen:int=1)->bytes:
    need(len(uid)==16 and uid!=bytes(16), 'bad UUID')
    need(volume_blocks>=MIN_BLOCKS and sector_bytes in (512,4096),'unsupported volume geometry')
    b=bytearray(BLOCK);b[:8]=BOOT_MAGIC
    struct.pack_into('<HHHH',b,8,1,0,256,12)
    b[16:32]=uid
    struct.pack_into('<QQQQ',b,32,volume_blocks,secrets.randbits(64) or 1,gen,1)
    struct.pack_into('<QII',b,64,ANCHOR_START,ANCHOR_SLOTS,1)
    struct.pack_into('<QQ',b,80,RECOVERY_START,RECOVERY_BLOCKS)
    struct.pack_into('<Q',b,96,LAYOUT_PBLK)
    struct.pack_into('<II',b,116,sector_bytes,1024)
    struct.pack_into('<I',b,124,1)
    b[128:160]=digest_masked(BOOT_DOMAIN,b,128,32)
    return bytes(b)


def check_bootstrap(b:bytes,volume_blocks:int,sector_bytes:int)->bytes:
    need(len(b)==BLOCK and b[:8]==BOOT_MAGIC,'bootstrap magic')
    need(struct.unpack_from('<HHHH',b,8)==(1,0,256,12),'bootstrap ABI')
    need(b[16:32]!=bytes(16),'bootstrap zero uuid')
    need(u64(b,32)==volume_blocks and volume_blocks>=MIN_BLOCKS,'bootstrap volume geometry')
    need(u64(b,40)>=1 and u64(b,48)>=1 and u64(b,56)>=1,'bootstrap nonce/generation/epoch')
    need((u64(b,64),u32(b,72),u32(b,76))==(ANCHOR_START,ANCHOR_SLOTS,1),'bootstrap ring')
    need((u64(b,80),u64(b,88),u64(b,96))==(RECOVERY_START,RECOVERY_BLOCKS,LAYOUT_PBLK),'bootstrap recovery')
    need(b[104:116]==bytes(12),'unsupported bootstrap feature')
    need((u32(b,116),u32(b,120),u32(b,124))==(sector_bytes,1024,1),'bootstrap feature geometry')
    need(b[160:]==bytes(BLOCK-160),'bootstrap reserved')
    need(b[128:160]==digest_masked(BOOT_DOMAIN,b,128,32),'bootstrap BLAKE3')
    return b[16:32]


def make_layout(uid:bytes,volume_blocks:int,data_start:int)->bytes:
    b=bytearray(BLOCK);b[:8]=LAYOUT_MAGIC
    struct.pack_into('<HHI',b,8,1,128,0)
    b[16:32]=uid
    # Global shard 0, physical rings and immutable metadata bounds.
    struct.pack_into('<QQQQQQ',b,32,volume_blocks,ANCHOR_START,ANCHOR_SLOTS,
                     RECOVERY_START,RECOVERY_BLOCKS,ROOT_FIRST)
    struct.pack_into('<Q',b,80,data_start)
    b[96:128]=digest_masked(LAYOUT_DOMAIN,b,96,32)
    return bytes(b)


def check_layout(b:bytes,uid:bytes,n:int)->int:
    need(len(b)==BLOCK and b[:8]==LAYOUT_MAGIC and b[16:32]==uid,'layout identity')
    need(struct.unpack_from('<HHI',b,8)==(1,128,0),'layout version')
    need(struct.unpack_from('<QQQQQQ',b,32)==(n,ANCHOR_START,ANCHOR_SLOTS,
                  RECOVERY_START,RECOVERY_BLOCKS,ROOT_FIRST),'layout location')
    d=u64(b,80)
    need(DATA_MIN<=d<n-3,'data extent region out of bounds')
    need(b[88:96]==bytes(8) and b[128:]==bytes(BLOCK-128),'layout nonzero reserved')
    need(b[96:128]==digest_masked(LAYOUT_DOMAIN,b,96,32),'layout checksum')
    return d


def oid(i:int)->bytes:
    need(0<i<2**64,'oid overflow')
    return struct.pack('<QQ',1,i)


def node(kind:int,entries:list[tuple[bytes,bytes,int]])->bytes:
    need(kind in (1,2,3,4,5),'unsupported tree kind')
    b=bytearray(BLOCK);b[:8]=NODE_MAGIC
    struct.pack_into('<HHHBB',b,8,1,kind,96,0,0)
    struct.pack_into('<Q',b,32,1) # birth seq
    prev=None;cursor=96
    for k,v,record_kind in sorted(entries,key=lambda x:x[0]):
        need(k and len(k)<=65535 and len(v)<=65535,'bad node key/value')
        need(prev is None or prev<k,'duplicate node key')
        need(0<record_kind<=65535,'unknown record kind')
        need(cursor+8+len(k)+len(v)<=BLOCK,'node full: build B+tree branch before larger install')
        struct.pack_into('<HHHH',b,cursor,len(k),len(v),record_kind,0);cursor+=8
        b[cursor:cursor+len(k)]=k;cursor+=len(k)
        b[cursor:cursor+len(v)]=v;cursor+=len(v)
        prev=k
    struct.pack_into('<II',b,40,len(entries),cursor-96)
    b[64:96]=digest_masked(NODE_DOMAIN,b,64,32)
    return bytes(b)


def parse_node(b:bytes,kind:int)->list[tuple[bytes,bytes,int]]:
    need(len(b)==BLOCK and b[:8]==NODE_MAGIC,'bad node magic')
    need(u16(b,8)==1 and u16(b,10)==kind and u16(b,12)==96 and b[14:16]==bytes(2),'node ABI')
    need(b[16:32]==bytes(16) and u64(b,32)>=1 and u64(b,48)==0 and u64(b,56)==0,'node header')
    need(b[64:96]==digest_masked(NODE_DOMAIN,b,64,32),'node digest')
    count=u32(b,40);size=u32(b,44)
    need(size<=4000 and count<=4000//9,'node count/size')
    pos=96;end=96+size;result=[];prev=None
    for _ in range(count):
        need(pos+8<=end,'node record header boundary')
        nk,nv,rkind,flags=struct.unpack_from('<HHHH',b,pos);pos+=8
        need(flags==0 and nk>0 and rkind>0,'node record fields')
        need(nk+nv<=end-pos,'node record payload boundary')
        key=bytes(b[pos:pos+nk]);pos+=nk
        val=bytes(b[pos:pos+nv]);pos+=nv
        need(prev is None or prev<key,'duplicate/unsorted node key')
        result.append((key,val,rkind));prev=key
    need(pos==end and b[end:]==bytes(BLOCK-end),'node tail')
    return result


def pointer(uid:bytes,pblk:int,blob:bytes,stored_len:int,kind:int,owner:bytes=bytes(16)) ->bytes:
    need(pblk>=ROOT_FIRST and kind in (1,2),'pointer physical address/kind')
    need(len(blob)%BLOCK==0 and stored_len>0 and stored_len<=len(blob),'pointer storage size')
    need(len(blob)//BLOCK<=0xffffffff and stored_len<=0xffffffff,'pointer size overflow')
    need(len(owner)==16,'pointer owner')
    b=bytearray(128)
    struct.pack_into('<QIII',b,0,pblk,len(blob)//BLOCK,stored_len,stored_len)
    struct.pack_into('<Q',b,28,1)
    b[36:52]=owner
    struct.pack_into('<HH',b,68,1,kind)
    b[72:104]=blake3(PTR_DOMAIN+uid+b[:72]+b[104:120]+blob[:stored_len])
    return bytes(b)


def check_pointer(b:bytes,uid:bytes,read,volume_blocks:int,kind:int)->bytes:
    need(len(b)==128,'pointer size')
    pos,n,stored,logical=struct.unpack_from('<QIII',b,0)
    need(pos>=ROOT_FIRST and n>0 and pos<=volume_blocks-3-n,'pointer extent geometry')
    need(0<stored<=n*BLOCK and logical==stored,'pointer content sizes')
    need(b[20:28]==bytes(8) and u64(b,28)>=1 and b[52:68]==bytes(16),'pointer unsupported flags/nonce')
    need((u16(b,68),u16(b,70))==(1,kind),'pointer hash/kind')
    need(b[104:]==bytes(24),'pointer auth/reserved')
    blob=b''.join(read(pos+i) for i in range(n))
    need(blob[stored:]==bytes(len(blob)-stored),'pointer nonzero padding')
    need(b[72:104]==blake3(PTR_DOMAIN+uid+b[:72]+b[104:120]+blob[:stored]),'pointer digest')
    return blob[:stored]


def object_value(objtype:int,parent:bytes,size:int,allocated:int,mode:int)->bytes:
    b=bytearray(144)
    b[0]=objtype
    struct.pack_into('<H',b,2,mode)
    b[8:24]=parent
    struct.pack_into('<QQQQQ',b,24,size,allocated,1,1,1)
    return bytes(b)


def layout_files(files:dict[str,bytes]):
    names={'/':(True,b'')}
    for path,blob in files.items():
        need(isinstance(blob,bytes),'file is not bytes')
        need(path.startswith('/') and path!='/' and len(path.encode('utf8'))<=4096,'absolute path required')
        need('\x00' not in path and '//' not in path and not path.endswith('/'),'invalid path')
        components=path.strip('/').split('/')
        need(all(p not in ('','.','..') and len(p.encode('utf8'))<=255 for p in components),'invalid name')
        current=''
        for segment in components[:-1]:
            current+='/'+segment;names.setdefault(current,(True,b''))
        need(path not in names,'duplicate file or directory')
        names[path]=(False,blob)
    return names


def _tlv(tag:int,value:bytes)->bytes:
    return struct.pack('<HHI',tag,0,len(value))+value


def make_descriptor(uid:bytes,root_set:bytes,layout:bytes)->bytes:
    # Deterministic canonical TLV tags from IFS boot profile v1.
    layout_record=struct.pack('<Q',LAYOUT_PBLK)+blake3(layout)
    body=b''.join([_tlv(1,root_set),_tlv(2,struct.pack('<Q',1)),
        _tlv(3,root_set[-128:]),_tlv(4,b''),_tlv(5,b''),
        _tlv(6,b''),_tlv(7,bytes(32)),_tlv(8,struct.pack('<Q',1)),
        _tlv(9,struct.pack('<Q',0)),_tlv(10,struct.pack('<Q',1)),
        _tlv(11,layout_record)])
    total=128+len(body)
    need(total<=BLOCK,'descriptor exceeds one boot profile block')
    b=bytearray(BLOCK);b[:8]=DESC_MAGIC
    struct.pack_into('<HHIQQQ',b,8,1,128,total,1,1,1)
    b[40:56]=uid
    b[56:88]=blake3(BODY_DOMAIN+body)
    b[128:total]=body
    return bytes(b)


def check_descriptor(b:bytes,uid:bytes,read,volume_blocks:int)->tuple[bytes,tuple[bytes,...]]:
    need(len(b)==BLOCK and b[:8]==DESC_MAGIC,'bad descriptor magic')
    need((u16(b,8),u16(b,10))==(1,128),'descriptor ABI')
    total=u32(b,12)
    need(128<=total<=BLOCK and b[40:56]==uid,'descriptor size/UUID')
    need((u64(b,16),u64(b,24),u64(b,32))==(1,1,1),'descriptor epoch/tx')
    need(b[88:128]==bytes(40) and b[total:]==bytes(BLOCK-total),'descriptor reserved')
    body=b[128:total]
    need(b[56:88]==blake3(BODY_DOMAIN+body),'descriptor body checksum')
    tags={};cur=0;prev=0
    while cur<len(body):
        need(len(body)-cur>=8,'tlv header truncated')
        tag,flags,length=struct.unpack_from('<HHI',body,cur);cur+=8
        need(flags==0 and tag>prev and length<=len(body)-cur,'tlv order/length')
        tags[tag]=body[cur:cur+length];cur+=length;prev=tag
    need(list(tags)==list(range(1,12)),'descriptor mandatory TLVs absent')
    root_set=tags[1]
    need(len(root_set)==520 and struct.unpack_from('<II',root_set,0)==(0,4),'root_set ABI')
    roots=tuple(root_set[8+i*128:8+(i+1)*128] for i in range(4))
    need(tags[2]==struct.pack('<Q',1) and tags[3]==roots[3],'descriptor root consistency')
    need(tags[4]==b'' and tags[5]==b'' and tags[6]==b'','unsupported GC/cross-shard')
    need(tags[7]==bytes(32) and tags[8]==struct.pack('<Q',1) and tags[9]==struct.pack('<Q',0),'descriptor policy')
    need(tags[10]==struct.pack('<Q',1),'recovery frontier')
    need(len(tags[11])==40 and u64(tags[11],0)==LAYOUT_PBLK,'layout pointer')
    layout=read(LAYOUT_PBLK)
    check_layout(layout,uid,volume_blocks)
    need(tags[11][8:]==blake3(layout),'layout payload digest')
    return root_set,roots


def make_seal(uid:bytes,desc:bytes,root_set:bytes)->bytes:
    b=bytearray(BLOCK);b[:8]=SEAL_MAGIC
    struct.pack_into('<HHBBH',b,8,1,256,2,2,0)
    b[16:32]=uid
    struct.pack_into('<QIIQQQQ',b,32,1,0xffffffff,0,1,0,1,1)
    struct.pack_into('<QII',b,80,DESCRIPTOR_PBLK,1,1)
    b[96:128]=blake3(DESC_DOMAIN+desc)
    b[160:192]=blake3(ROOT_DOMAIN+root_set)
    struct.pack_into('<QII',b,200,ANCHOR_START,0,1)
    b[216:248]=blake3(SEAL_DOMAIN+bytes(b))
    tmp=bytearray(b);tmp[208:212]=bytes(4)
    struct.pack_into('<I',b,208,crc32c(bytes(tmp)))
    return bytes(b)


def check_seal(b:bytes,uid:bytes,desc:bytes,root_set:bytes,position:int)->None:
    need(len(b)==BLOCK and b[:8]==SEAL_MAGIC,'seal magic')
    need((u16(b,8),u16(b,10),b[12],b[13],u16(b,14))==(1,256,2,2,0),'seal ABI')
    need(b[16:32]==uid and u64(b,32)==1 and u32(b,40)==0xffffffff and u32(b,44)==0,'seal identity')
    need((u64(b,48),u64(b,56),u64(b,64),u64(b,72))==(1,0,1,1),'seal chain')
    need((u64(b,80),u32(b,88),u32(b,92))==(DESCRIPTOR_PBLK,1,1),'seal descriptor location')
    need(b[96:128]==blake3(DESC_DOMAIN+desc),'seal descriptor digest')
    need(b[128:160]==bytes(32) and b[160:192]==blake3(ROOT_DOMAIN+root_set),'seal root chain')
    need(u64(b,192)==0 and u64(b,200)==position and u32(b,212)==1,'seal slot/hash')
    need(b[248:]==bytes(BLOCK-248),'seal reserved bytes')
    masked=bytearray(b);masked[208:212]=bytes(4)
    need(u32(b,208)==crc32c(bytes(masked)),'seal CRC32C')
    masked[216:248]=bytes(32)
    need(b[216:248]==blake3(SEAL_DOMAIN+bytes(masked)),'seal BLAKE3')


@dataclass(frozen=True)
class IFSFile:
    path:str
    type:str
    size:int
    data:bytes|None


class Volume:
    def __init__(self,filename:Path,part_byte_start:int,part_bytes:int,sector_bytes:int=512):
        self.filename=Path(filename)
        self.start=part_byte_start
        self.length=part_bytes
        self.sector_bytes=sector_bytes
        need(part_byte_start>=0 and part_bytes%BLOCK==0 and part_byte_start%sector_bytes==0,
             'partition geometry/sector alignment')
        need(part_bytes//BLOCK>=MIN_BLOCKS and sector_bytes in (512,4096),'volume minimum geometry')
        self.blocks=part_bytes//BLOCK
        need(self.filename.is_file() and self.filename.stat().st_size>=part_byte_start+part_bytes,
             'regular image missing/truncated')
        need(not self.filename.is_symlink() and self.filename.stat().st_nlink==1,
             'refuse symlink/hardlink targets')
    def _read(self,pblk:int)->bytes:
        need(0<=pblk<self.blocks,'read out of volume')
        with self.filename.open('rb') as f:
            f.seek(self.start+pblk*BLOCK)
            b=f.read(BLOCK)
            need(len(b)==BLOCK,'short read')
            return b
    def _write(self,f:BinaryIO,pblk:int,b:bytes):
        need(0<=pblk<self.blocks and len(b)==BLOCK,'out-of-bounds write')
        f.seek(self.start+pblk*BLOCK);f.write(b)
    def format(self,files:dict[str,bytes],*,volume_uuid:uuid.UUID|None=None):
        """Construct one complete initial CoW state, then publish its seal last."""
        items=layout_files(files)
        # Never silently reformat an existing or interrupted IFS volume.
        for pblk in (1,2,self.blocks-3,self.blocks-2):
            if self._read(pblk)[:8]==BOOT_MAGIC:
                raise IFSError('existing IFS bootstrap: refuse in-place reformat')
        uid=(volume_uuid or uuid.uuid4()).bytes
        data_start=max(DATA_MIN,((self.blocks*5+99)//100+7)//8*8)
        need(data_start<self.blocks-3,'volume reserve exceeds geometry')
        oids={name:oid(i+1) for i,name in enumerate(sorted(items))}
        oid_entries=[];dir_entries=[];extent_entries=[];all_blocks={}
        cursor=data_start
        for path,(directory,data) in sorted(items.items()):
            parent='/' if path=='/' else path.rsplit('/',1)[0] or '/'
            ident=oids[path]
            allocation=0
            if not directory and data:
                length=(len(data)+BLOCK-1)//BLOCK
                need(cursor+length<self.blocks-3,'out of space')
                payload=data.ljust(length*BLOCK,b'\0')
                all_blocks.update({cursor+i:payload[i*BLOCK:(i+1)*BLOCK] for i in range(length)})
                ptr=pointer(uid,cursor,payload,len(data),1,ident)
                extent_entries.append((ident+struct.pack('<Q',0),ptr,3))
                cursor+=length;allocation=length*BLOCK
            mode=(0o755 if directory or path.endswith('.ivx') else
                  0o600 if path=='/etc/system.json' else 0o644)
            oid_entries.append((ident,object_value(1 if directory else 2,oids[parent],len(data),allocation,mode),1))
            if path!='/':
                basename=path.rsplit('/',1)[-1].encode('utf8')
                dir_entries.append((oids[parent]+basename,ident,2))
        free_entries=[(struct.pack('<Q',cursor),struct.pack('<Q',self.blocks-3-cursor),4)]
        node_data=[node(1,oid_entries),node(2,dir_entries),node(3,extent_entries),node(4,free_entries)]
        root_set=struct.pack('<II',0,4)+b''.join(pointer(uid,ROOT_FIRST+i,x,BLOCK,2) for i,x in enumerate(node_data))
        layout=make_layout(uid,self.blocks,data_start)
        desc=make_descriptor(uid,root_set,layout)
        seal=make_seal(uid,desc,root_set)
        bootstrap=make_bootstrap(uid,self.blocks,self.sector_bytes)
        # Write all reachable immutable material, flush before any seal.
        with self.filename.open('r+b',buffering=0) as f:
            for pblk,blob in all_blocks.items():self._write(f,pblk,blob)
            for i,b in enumerate(node_data):self._write(f,ROOT_FIRST+i,b)
            self._write(f,LAYOUT_PBLK,layout)
            self._write(f,DESCRIPTOR_PBLK,desc)
            f.flush();os.fsync(f.fileno())
            # Four boot copies can be written before publish. No live root yet.
            for pblk in (1,2,self.blocks-3,self.blocks-2):self._write(f,pblk,bootstrap)
            f.flush();os.fsync(f.fileno())
            self._write(f,ANCHOR_START,seal)
            f.flush();os.fsync(f.fileno())
        return {'volume_uuid':str(uuid.UUID(bytes=uid)), 'filesystem':'IntellectFS-1',
                'files':len(files),'directories':sum(d for d,_ in items.values()),
                'committed_seal_pblk':ANCHOR_START,'allocated_data_blocks':cursor-data_start}

    def check(self,paths:bool=True)->dict[str,IFSFile]:
        versions=[]
        for pos in (1,2,self.blocks-3,self.blocks-2):
            try:versions.append(check_bootstrap(self._read(pos),self.blocks,self.sector_bytes))
            except IFSError:pass
        need(versions,'no valid bootstrap')
        need(all(u==versions[0] for u in versions),'bootstrap split-brain UUID')
        uid=versions[0]
        desc=self._read(DESCRIPTOR_PBLK)
        roots,root_pointers=check_descriptor(desc,uid,self._read,self.blocks)
        check_seal(self._read(ANCHOR_START),uid,desc,roots,ANCHOR_START)
        parsed=[parse_node(check_pointer(ptr,uid,self._read,self.blocks,2),kind)
                for kind,ptr in enumerate(root_pointers,1)]
        objects={key:val for key,val,k in parsed[0] if k==1}
        need(len(objects)==len(parsed[0]),'duplicate/incorrect object records')
        dirs={key:val for key,val,k in parsed[1] if k==2}
        need(len(dirs)==len(parsed[1]),'bad directory records')
        extents={key:val for key,val,k in parsed[2] if k==3}
        need(len(extents)==len(parsed[2]),'bad extent records')
        need(len(parsed[3])==1 and parsed[3][0][2]==4,'free-space tree')
        start=u64(parsed[3][0][0],0);length=u64(parsed[3][0][1],0)
        need(start>=DATA_MIN and start+length==self.blocks-3,'free-space head/tail')
        paths_by_oid={}
        rootident=None
        for ident,record in objects.items():
            need(len(ident)==16 and len(record)==144 and record[0] in (1,2),'bad object record')
            need(record[1]==0 and record[4:8]==bytes(4) and record[136:144]==bytes(8),'object reserved')
            if ident==record[8:24] and record[0]==1:
                need(rootident is None,'multiple roots')
                rootident=ident;paths_by_oid[ident]='/'
        need(rootident is not None,'root missing')
        for _ in range(len(objects)):
            for ident,record in objects.items():
                if ident in paths_by_oid:continue
                parent=record[8:24]
                if parent not in paths_by_oid:continue
                keys=[key[16:].decode('utf8') for key,v in dirs.items()
                      if key[:16]==parent and v==ident and len(key)>16]
                need(len(keys)==1 and '/' not in keys[0] and '\x00' not in keys[0],'directory entry conflict')
                prefix=paths_by_oid[parent]
                paths_by_oid[ident]=(prefix.rstrip('/')+'/'+keys[0])
        need(len(paths_by_oid)==len(objects) and len(dirs)==len(objects)-1,'orphan/cyclic object')
        results={};used=set(range(0,DATA_MIN))
        for ident,record in objects.items():
            path=paths_by_oid[ident];directory=record[0]==1
            size=u64(record,24)
            if directory:
                need(size==0 and not any(k[:16]==ident for k in extents),'directory extent')
                results[path]=IFSFile(path,'dir',0,None);continue
            key=ident+struct.pack('<Q',0)
            blob=b''
            if size:
                need(key in extents,'missing file extent')
                ptr=extents[key]
                pblk=u64(ptr,0);blocks=u32(ptr,8)
                need(pblk>=DATA_MIN and pblk+blocks<=start,'file outside committed allocation region')
                need(not set(range(pblk,pblk+blocks))&used,'shared/overlap file data not supported')
                used.update(range(pblk,pblk+blocks))
                blob=check_pointer(ptr,uid,self._read,self.blocks,1)
                need(size==len(blob),'file logical size differs from pointer')
            else:
                need(key not in extents,'unexpected zero-sized file pointer')
            results[path]=IFSFile(path,'file',size,blob if paths else None)
        need(len(extents)==sum(1 for r in results.values() if r.type=='file' and r.size),
             'dangling extent entries')
        need(len(used-set(range(DATA_MIN)))==start-max(DATA_MIN,((self.blocks*5+99)//100+7)//8*8),
             'allocation bitmap not consistent')
        return results


def format_partition_image(filename:Path,start_lba:int,count_lba:int,files:dict[str,bytes],
                           sector_bytes:int=512):
    need(count_lba*sector_bytes%BLOCK==0 and start_lba*sector_bytes%BLOCK==0,
         'IFS partition not aligned to 4 KiB')
    v=Volume(filename,start_lba*sector_bytes,count_lba*sector_bytes,sector_bytes)
    result=v.format(files)
    verify=v.check()
    need({p for p,m in verify.items() if m.type=='file'}==set(files),'install file set mismatch')
    for p,expected in files.items():need(verify[p].data==expected,'install readback mismatch: '+p)
    return result
