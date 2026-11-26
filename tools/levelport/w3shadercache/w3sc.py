"""Witcher 3 DX12 shader cache (shaderdx12_0.cache): data region of blobs (u64 hash, u8 flag, u32 ulen, u32 clen,
zlib), then an index region (footer: ..., u64 index offset, u64 index size, u64 data size, 'RDHS', u32 5)."""
import os, struct, sys, zlib
F = os.path.join(os.environ.get('W3_GAME', r'<Steam library>/steamapps/common/The Witcher 3'), 'content', 'content0',
                 'shaderdx12_0.cache')

def load():
    data = open(F, 'rb').read()
    ioff, isz = struct.unpack_from('<QQ', data, len(data) - 32)
    blobs = {}
    p = 0
    while p < ioff:
        h, fl, ul, cl = struct.unpack_from('<QBII', data, p)
        if fl != 1 or cl == 0 or cl > (64 << 20):
            break
        blobs[h] = (p + 17, cl, ul)
        p += 17 + cl
    return data, ioff, isz, blobs, p

if __name__ == '__main__':
    data, ioff, isz, blobs, end = load()
    print('index at', ioff, 'size', isz, 'blobs', len(blobs), 'walk ended at', end)
    idx = data[ioff:ioff + isz]
    hits = 0
    for h in list(blobs)[:2000]:
        if idx.find(struct.pack('<Q', h)) >= 0: hits += 1
    print('of 2000 blob hashes found in index:', hits)
