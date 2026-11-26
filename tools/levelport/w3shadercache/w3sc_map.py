import re, struct, sys, zlib, os, collections
sys.path.insert(0, os.path.dirname(__file__))
import w3sc
data, ioff, isz, blobs, _ = w3sc.load()
idx = data[ioff:ioff + isz]
paths = [(m.start(), m.end(), m.group().decode('latin1')) for m in re.finditer(rb'[a-z0-9_.\x5c]{3,200}\.w2m[gi]', idx)]
out = sys.argv[1]
for name in sys.argv[2:]:
    hs = collections.Counter()
    for k, (s, e, p) in enumerate(paths):
        if not p.endswith(name): continue
        nxt = paths[k + 1][0] if k + 1 < len(paths) else len(idx)
        seg = idx[e:min(nxt, e + 4096)]
        for i in range(len(seg) - 7):
            h = struct.unpack_from('<Q', seg, i)[0]
            if h in blobs: hs[h] += 1
    d = os.path.join(out, name.split('.')[0]); os.makedirs(d, exist_ok=True)
    for h in hs:
        o, cl, ul = blobs[h]
        u = zlib.decompress(data[o:o + cl]); k = u.find(b'DXBC')
        open(os.path.join(d, '%016x.dxbc' % h), 'wb').write(u[k:])
    print(name, sum(1 for p in paths if p[2].endswith(name)), 'entries', len(hs), 'blobs')
