"""The Witcher 3's bundles, read only: index() and read() fetch any depot file straight from the game's .bundle files
(no uncook, no Blender). Moved here from a scratch reader, for w3emitter.py.

A bundle: "POTATO70", u32 bundle size, u32 dummy, u32 data offset (the table's size), u16 version at 20; from byte 32 a
table of 320 B (version 3) or 304 B (version 5) rows: 256 B name, 16 B hash, then the sizes, offset and compression
(0 none, 1 zlib, 2 snappy, 3 doboz, 4/5 lz4). A file in several bundles: a patch bundle's copy wins.
The game's folder is $W3_GAME (or $W3_ROOT) unless given; the doboz-compressed files need the add-on's Doboz.dll as
$W3_DOBOZ (w3paths.py).
    python tools/levelport/w3bundle.py <depot path> <out file>"""
import ctypes
import glob
import os
import pickle
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w3paths  # noqa: E402

_doboz = None
CACHE_VERSION = 2


def _bundles(root):
    """The game's bundle files and their (size, mtime): the cache's stamp"""
    out = []
    for b in sorted(glob.glob(os.path.join(w3paths.game(root), '**', '*.bundle'), recursive=True)):
        st = os.stat(b)
        out.append((b, st.st_size, st.st_mtime_ns))
    return out


def index(root=None, suffix=None, cache=None):
    """name (lower case, backslashes) -> [(bundle path, offset, size, packed size, compression)]; `suffix` keeps only
    names ending with it ('.w2p'); `cache` is a pickle read if it exists and was made from the same bundle files (path,
    size, mtime), else written (it holds the unfiltered index)"""
    stamp = _bundles(root)
    idx = None
    if cache and os.path.exists(cache):
        with open(cache, 'rb') as f:
            c = pickle.load(f)
        if isinstance(c, dict) and c.get('version') == CACHE_VERSION and c.get('bundles') == stamp:
            idx = c['index']
        else:
            print('w3bundle: %s was made from other bundle files: indexing again' % cache, file=sys.stderr)
    if idx is None:
        idx = {}
        for b, _size, _mtime in stamp:
            with open(b, 'rb') as f:
                pre = f.read(32)
                if pre[:8] != b'POTATO70':
                    continue
                ver = struct.unpack_from('<H', pre, 20)[0]
                es = {3: 320, 5: 304}.get(ver)
                if es is None:
                    continue
                toc = f.read(struct.unpack_from('<I', pre, 16)[0])
            for i in range(0, len(toc) - es + 1, es):
                row = toc[i:i + es]
                name = row[:0x100].split(b'\0', 1)[0].decode('latin-1')
                if ver == 5:
                    off, size, zsize, _crc, comp = struct.unpack_from('<QIIIB', row, 0x110)
                else:
                    _empty, size, zsize, off, _d, _t = struct.unpack_from('<6I', row, 0x110)
                    _crc, comp = struct.unpack_from('<2I', row, 0x138)
                idx.setdefault(name.lower(), []).append((b, off, size, zsize, comp))
        if cache:
            with open(cache + '.tmp', 'wb') as f:
                pickle.dump({'version': CACHE_VERSION, 'bundles': stamp, 'index': idx}, f)
            os.replace(cache + '.tmp', cache)
    if suffix:
        idx = {k: v for k, v in idx.items() if k.endswith(suffix)}
    return idx


def read_entry(e):
    global _doboz
    b, off, size, zsize, comp = e
    with open(b, 'rb') as f:
        f.seek(off)
        raw = f.read(zsize)
    if comp == 0:
        return raw
    if comp == 1:
        return zlib.decompress(raw)
    if comp in (4, 5):
        import lz4.block
        return lz4.block.decompress(raw, uncompressed_size=size)
    if comp == 3:
        if _doboz is None:
            _doboz = ctypes.CDLL(w3paths.doboz())
            _doboz.Decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
            _doboz.Decompress.restype = ctypes.c_int
        dst = ctypes.create_string_buffer(size)
        r = _doboz.Decompress(ctypes.create_string_buffer(raw, zsize), zsize, dst, size)
        if r != 0:
            raise ValueError('doboz %d' % r)
        return dst.raw[:size]
    if comp == 2:
        import cramjam
        return bytes(cramjam.snappy.decompress_raw(raw, output_len=size))
    raise ValueError('compression %d' % comp)


def pick(entries):
    """A patch bundle's copy (the latest patch) before the others"""
    def key(e):
        p = e[0].lower()
        return (('patch' in p), p)
    return sorted(entries, key=key)[-1]


def read(idx, name):
    """The file's bytes (None: not in the bundles)"""
    e = idx.get(name.lower().replace('/', chr(92)))
    return read_entry(pick(e)) if e else None


if __name__ == '__main__':
    import sys
    data = read(index(), sys.argv[1])
    if data is None:
        sys.exit('not in the bundles: ' + sys.argv[1])
    with open(sys.argv[2], 'wb') as out:
        out.write(data)
