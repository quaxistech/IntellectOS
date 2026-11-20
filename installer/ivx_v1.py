"""IntellectOS IVX-1 host-side single-segment signed EXEC profile.
Reference: docs/05_IVX_BINARY_EXECUTABLE_SPEC.md, sections 3,8,27.
DRAFT subset: no MANIFEST or Closure Record, therefore NOT runnable under
production IVX trust policy. Never silently accept unsigned/untrusted bytes.
"""
from __future__ import annotations
from dataclasses import dataclass
import struct
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.exceptions import InvalidSignature
from .blake3_ref import blake3

MAGIC = b'IVX\x00\r\n\x1a\n'
HEADER_BYTES = 128
SEGMENT_BYTES = 64
DOMAIN_SIGN = b'IVX1/SIGN\0' + struct.pack('<H', 1)
# Provisional canonical public-key representation, algorithm ID 1 (Ed25519).
KEY_ID_DOMAIN = b'IVX1/KEY/ED25519\0'

class IVXError(ValueError):
    pass

def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from('<H', data, offset)[0]

def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from('<I', data, offset)[0]

def _u64(data: bytes, offset: int) -> int:
    return struct.unpack_from('<Q', data, offset)[0]

def _checked(offset: int, size: int, limit: int, label: str) -> None:
    if offset < 0 or size < 0 or offset > limit or size > limit - offset:
        raise IVXError(f'{label}: range outside file')

def merkle_nodes(signed_region: bytes) -> tuple[list[bytes], bytes]:
    if not signed_region:
        raise IVXError('empty signed region')
    masked = bytearray(signed_region)
    if len(masked) < 128:
        raise IVXError('truncated header')
    masked[96:128] = b'\0' * 32
    leaves = []
    for i, off in enumerate(range(0, len(masked), 4096)):
        block = bytes(masked[off:off+4096])
        leaves.append(blake3(b'IVX1/LEAF\0' + struct.pack('<QI', i, len(block)) + block))
    all_nodes = list(leaves)
    level = 1
    while len(leaves) > 1:
        parents = []
        for idx, start in enumerate(range(0, len(leaves), 16)):
            kids = leaves[start:start+16]
            parents.append(blake3(b'IVX1/NODE\0' + struct.pack('<IQB', level, idx,
                                                               len(kids)) + b''.join(kids)))
        all_nodes.extend(parents)
        leaves = parents
        level += 1
    return all_nodes, leaves[0]

def public_raw(priv: Ed25519PrivateKey) -> bytes:
    return priv.public_key().public_bytes(serialization.Encoding.Raw,
                                          serialization.PublicFormat.Raw)

def key_id(pub: bytes) -> bytes:
    if len(pub) != 32:
        raise IVXError('Ed25519 public key width')
    return blake3(KEY_ID_DOMAIN + pub)

@dataclass(frozen=True)
class VerifiedIVX:
    entry_rva: int
    code_rva: int
    code: bytes
    pubkey_id: bytes
    merkle_root: bytes


def build_dev_terminal(code: bytes, private_key: Ed25519PrivateKey) -> bytes:
    """Signed structural fixture, not an approved production IVX executable.

    16-byte NOP prefix keeps entry_rva!=0 and allows the original U1 flat
    user program to execute at a nonzero RVA once a genuine loader exists.
    """
    if not code or len(code) + 16 > 4096:
        raise IVXError('bootstrap code must fit one 4-KiB page')
    payload = b'\x90' * 16 + code
    header = bytearray(128)
    header[:8] = MAGIC
    struct.pack_into('<HHHBB', header, 8, 128, 1, 0, 1, 1)
    struct.pack_into('<IIHHBBBB', header, 16, 0, 0, 1, 0, 12, 12, 1, 0)
    struct.pack_into('<Q', header, 32, 16)  # EXEC entry_rva
    signed_len = 128 + 64 + len(payload)
    struct.pack_into('<QQIIQIIQ', header, 40,
                     signed_len, 128, 1, 64, 192, 0, 32, signed_len)
    seg = bytearray(64)
    struct.pack_into('<QQQQQIIIIQ', seg, 0,
                     192, len(payload), 0, len(payload), 16,
                     5, 0, 1, 0xffffffff, 0)
    region = bytes(header) + bytes(seg) + payload
    nodes, root = merkle_nodes(region)
    header[96:128] = root
    region = bytes(header) + bytes(seg) + payload
    merkle = b''.join(nodes)
    # Exact IVXS profile (section 27): header + one record + raw key id + signature.
    public = public_raw(private_key)
    ident = key_id(public)
    signature = private_key.sign(DOMAIN_SIGN + root)
    env = bytearray(32+24+32+64)
    struct.pack_into('<4sHHIIIIQ', env, 0, b'IVXS', 1, 0, len(env), 1, 24, 0, 0)
    struct.pack_into('<HHIIIII', env, 32, 1, 0, 56, 32, 88, 64, 0)
    env[56:88] = ident
    env[88:152] = signature
    struct.pack_into('<Q', header, 88, len(merkle))
    # merkle_bytes header change affects root: redo in correct order.
    region = bytes(header) + bytes(seg) + payload
    nodes, root = merkle_nodes(region)
    header[96:128] = root
    env[88:152] = private_key.sign(DOMAIN_SIGN + root)
    region = bytes(header) + bytes(seg) + payload
    return region + b''.join(nodes) + bytes(env)


def verify_dev_terminal(blob: bytes, trusted_public_key: bytes) -> VerifiedIVX:
    """Refuse unknown formats and verify full Merkle and Ed25519 trust.

    This bounded developer profile is not a general IVX Loader: it has only
    one executable segment, no manifest, no relocations or imports.
    """
    if not isinstance(blob, (bytes, bytearray)) or len(blob) > 1 << 20 or len(blob) < 352:
        raise IVXError('invalid total length')
    if blob[:8] != MAGIC:
        raise IVXError('bad IVX magic')
    if (_u16(blob, 8), _u16(blob, 10), _u16(blob, 12)) != (128, 1, 0):
        raise IVXError('unsupported version/header')
    if blob[14:16] != b'\x01\x01' or _u32(blob, 16) or _u32(blob, 20):
        raise IVXError('unsupported image kind, ISA or features')
    if (_u16(blob, 24),_u16(blob, 26), blob[28:32]) != (1, 0, bytes([12,12,1,0])):
        raise IVXError('unsupported ABI/page/hash')
    if (_u32(blob, 56),_u32(blob, 60), _u64(blob, 48)) != (1,64,128):
        raise IVXError('bad segment table')
    if (_u64(blob, 64),_u32(blob, 72),_u32(blob, 76)) != (192,0,32):
        raise IVXError('unsupported directory')
    signed_len = _u64(blob, 40)
    merkle_off = _u64(blob, 80)
    merkle_size = _u64(blob, 88)
    if signed_len != merkle_off or signed_len < 208 or signed_len > (1<<18):
        raise IVXError('bad signed region length')
    _checked(merkle_off, merkle_size, len(blob), 'Merkle')
    if (fs := _u64(blob,128)) != 192:
        raise IVXError('unexpected file offset')
    length = _u64(blob,136)
    if _u64(blob,144) != 0 or _u64(blob,152) != length or _u64(blob,160)!=16:
        raise IVXError('unexpected segment geometry')
    if (_u32(blob,168),_u32(blob,172),_u32(blob,176),_u32(blob,180),_u64(blob,184)) != (5,0,1,0xffffffff,0):
        raise IVXError('unexpected permissions or segment type')
    if length < 17 or length > 4096 or 192+length != signed_len or _u64(blob,32) != 16:
        raise IVXError('unexpected entry or payload bounds')
    payload = blob[192:signed_len]
    if payload[:16] != b'\x90'*16:
        raise IVXError('entry padding changed')
    nodes, root = merkle_nodes(blob[:signed_len])
    if len(nodes)*32 != merkle_size or b''.join(nodes) != blob[merkle_off:merkle_off+merkle_size]:
        raise IVXError('Merkle path invalid')
    if blob[96:128] != root:
        raise IVXError('Merkle root mismatch')
    off = merkle_off+merkle_size
    _checked(off, 152, len(blob), 'IVXS')
    if off+152 != len(blob):
        raise IVXError('unexpected overlay')
    env = blob[off:]
    if struct.unpack_from('<4sHHIIIIQ', env, 0) != (b'IVXS',1,0,152,1,24,0,0):
        raise IVXError('invalid IVXS envelope')
    if struct.unpack_from('<HHIIIII', env, 32) != (1,0,56,32,88,64,0):
        raise IVXError('invalid IVXS record')
    ident = key_id(trusted_public_key)
    if env[56:88] != ident:
        raise IVXError('untrusted signature key identifier')
    try:
        Ed25519PublicKey.from_public_bytes(trusted_public_key).verify(env[88:152],DOMAIN_SIGN+root)
    except (InvalidSignature, ValueError) as exc:
        raise IVXError('signature invalid') from exc
    return VerifiedIVX(16,0,payload,ident,root)
