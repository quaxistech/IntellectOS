"""Read-only IntellectFS v1 bootstrap inspector, per design section 6.
NO mkifs, NO initial seal, NO RW mount: the format is still draft.
"""
from __future__ import annotations
from dataclasses import dataclass
import struct
import uuid
from .blake3_ref import blake3

BLOCK=4096
MAGIC=b'IFSB0001'
DOMAIN=b'IFS/bootstrap/v1\0'

class IFSBootstrapError(ValueError):pass

@dataclass(frozen=True)
class IFSBootstrap:
    volume_uuid: uuid.UUID
    volume_blocks: int
    generation: int
    writer_epoch_floor: int
    anchor_first: int
    anchor_slots: int
    anchor_stride: int
    recovery_first: int
    recovery_blocks: int


def parse_bootstrap(data:bytes, volume_blocks:int, sector_bytes:int=512)->IFSBootstrap:
    if len(data)!=BLOCK or data[:8]!=MAGIC:
        raise IFSBootstrapError('bad bootstrap magic or length')
    if struct.unpack_from('<HHHH',data,8)!=(1,0,256,12):
        raise IFSBootstrapError('unsupported IFS wire layout')
    if any(data[160:]):raise IFSBootstrapError('reserved bytes not zero')
    if any(struct.unpack_from('<III',data,104)):
        raise IFSBootstrapError('required/ro/compat features unsupported')
    if struct.unpack_from('<I',data,116)[0]!=sector_bytes:
        raise IFSBootstrapError('device sector size mismatch')
    if volume_blocks<65536 or struct.unpack_from('<Q',data,32)[0]!=volume_blocks:
        raise IFSBootstrapError('volume geometry mismatch')
    masked=bytearray(data);masked[128:160]=bytes(32)
    if blake3(DOMAIN+masked)!=data[128:160]:
        raise IFSBootstrapError('bootstrap BLAKE3 mismatch')
    uid=uuid.UUID(bytes=data[16:32])
    if uid.int==0:raise IFSBootstrapError('zero volume uuid')
    generation=struct.unpack_from('<Q',data,48)[0]
    epoch=struct.unpack_from('<Q',data,56)[0]
    anchor=struct.unpack_from('<Q',data,64)[0]
    slots,stride=struct.unpack_from('<II',data,72)
    recovery,recovery_blocks=struct.unpack_from('<QQ',data,80)
    if not 1<=generation or not slots or not stride or not recovery_blocks:
        raise IFSBootstrapError('missing mandatory geometry')
    anchor_end=anchor+(slots-1)*stride+1
    recovery_end=recovery+recovery_blocks
    if (anchor<3 or recovery<3 or anchor_end>volume_blocks-3 or
        recovery_end>volume_blocks-3 or not (anchor_end<=recovery or recovery_end<=anchor)):
        raise IFSBootstrapError('anchor/recovery overlap or exceeds volume')
    return IFSBootstrap(uid,volume_blocks,generation,epoch,anchor,slots,stride,
                        recovery,recovery_blocks)


def inspect_four(read_block,volume_blocks:int,sector_bytes:int=512)->tuple[IFSBootstrap,...]:
    """Read four mandatory locations; reject inconsistent valid UUID copies."""
    valid=[]
    for pblk in (1,2,volume_blocks-3,volume_blocks-2):
        try:
            parsed=parse_bootstrap(read_block(pblk),volume_blocks,sector_bytes)
        except IFSBootstrapError:
            continue
        valid.append(parsed)
    if valid and any(v.volume_uuid!=valid[0].volume_uuid for v in valid):
        raise IFSBootstrapError('bootstrap split-brain UUID')
    return tuple(valid)
