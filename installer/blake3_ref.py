"""Portable independent BLAKE3-256 reference, pure Python; host tools only.
Based on the public BLAKE3 specification (CC0/Apache-2.0/MIT reference semantics).
Not constant-time and not suitable for kernel cryptographic primitives.
"""
from __future__ import annotations
from dataclasses import dataclass
import struct

MASK = 0xffffffff
IV = (0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
      0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19)
PERM = (2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8)
CHUNK_START = 1
CHUNK_END = 2
PARENT = 4
ROOT = 8


def ror32(w: int, n: int) -> int:
    return (w >> n) | ((w << (32 - n)) & MASK)


def compress(cv: tuple[int, ...], words: tuple[int, ...], counter: int,
             block_len: int, flags: int) -> tuple[int, ...]:
    s = list(cv) + list(IV[:4]) + [counter & MASK, (counter >> 32) & MASK,
                                   block_len, flags]
    msg = tuple(words)
    for _ in range(7):
        def g(a: int, b: int, c: int, d: int, x: int, y: int) -> None:
            s[a] = (s[a] + s[b] + x) & MASK
            s[d] = ror32(s[d] ^ s[a], 16)
            s[c] = (s[c] + s[d]) & MASK
            s[b] = ror32(s[b] ^ s[c], 12)
            s[a] = (s[a] + s[b] + y) & MASK
            s[d] = ror32(s[d] ^ s[a], 8)
            s[c] = (s[c] + s[d]) & MASK
            s[b] = ror32(s[b] ^ s[c], 7)
        g(0,4,8,12,msg[0],msg[1]);g(1,5,9,13,msg[2],msg[3])
        g(2,6,10,14,msg[4],msg[5]);g(3,7,11,15,msg[6],msg[7])
        g(0,5,10,15,msg[8],msg[9]);g(1,6,11,12,msg[10],msg[11])
        g(2,7,8,13,msg[12],msg[13]);g(3,4,9,14,msg[14],msg[15])
        msg = tuple(msg[i] for i in PERM)
    return tuple((s[i] ^ s[i + 8]) & MASK for i in range(8)) + \
           tuple((s[i + 8] ^ cv[i]) & MASK for i in range(8))


def block_words(data: bytes) -> tuple[int, ...]:
    assert len(data) <= 64
    return struct.unpack('<16I', data.ljust(64, b'\0'))

@dataclass(frozen=True)
class Output:
    cv: tuple[int, ...]
    words: tuple[int, ...]
    counter: int
    block_len: int
    flags: int

    def chain(self) -> tuple[int, ...]:
        return compress(self.cv, self.words, self.counter,
                        self.block_len, self.flags)[:8]

    def root(self) -> bytes:
        return struct.pack('<16I', *compress(self.cv, self.words, 0,
                                             self.block_len, self.flags | ROOT))[:32]


def chunk_output(chunk: bytes, index: int) -> Output:
    cv = IV
    blocks = [chunk[i:i+64] for i in range(0, len(chunk), 64)] or [b'']
    for i, block in enumerate(blocks):
        flags = (CHUNK_START if i == 0 else 0) | \
                (CHUNK_END if i == len(blocks) - 1 else 0)
        out = Output(cv, block_words(block), index, len(block), flags)
        if i + 1 != len(blocks):
            cv = out.chain()
    return out


def parent_output(left: tuple[int, ...], right: tuple[int, ...]) -> Output:
    return Output(IV, tuple(left + right), 0, 64, PARENT)


def blake3(data: bytes) -> bytes:
    chunks = [data[i:i+1024] for i in range(0, len(data), 1024)] or [b'']
    def sub(lo: int, hi: int) -> Output:
        if hi - lo == 1:
            return chunk_output(chunks[lo], lo)
        # Canonical binary subtree split: largest power of 2 < chunk count.
        count = hi - lo
        left_count = 1 << ((count - 1).bit_length() - 1)
        left = sub(lo, lo + left_count).chain()
        right = sub(lo + left_count, hi).chain()
        return parent_output(left, right)
    return sub(0, len(chunks)).root()
