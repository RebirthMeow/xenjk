"""packio.py -- the pack rewrite and the image helpers the character pack tools share.

rewrite() writes a copy of an open pk3 with some entries replaced, dropped or added: every kept entry in its order
with its time, compression and attributes, written to OUT.tmp and moved over OUT once complete (an interrupted run
leaves OUT as it was). box_down() is the engine's 2x2 box filter for mip chains.
"""
import os
import time
import zipfile

import numpy as np


def rewrite(z, out, replace=None, add=(), skip=None):
    """OUT = the open ZipFile z with replace {name: bytes} in place of those entries, entries whose name skip(name)
    is true left out, and add [(name, bytes)] appended stored, dated now."""
    tmp = out + '.tmp'
    with zipfile.ZipFile(tmp, 'w') as zo:
        for i in z.infolist():
            if skip is not None and skip(i.filename):
                continue
            zi = zipfile.ZipInfo(i.filename, date_time=i.date_time)
            zi.compress_type = i.compress_type
            zi.external_attr = i.external_attr
            zo.writestr(zi, replace[i.filename] if replace and i.filename in replace else z.read(i))
        for name, data in add:
            zi = zipfile.ZipInfo(name, date_time=time.localtime()[:6])
            zi.compress_type = zipfile.ZIP_STORED
            zo.writestr(zi, data)
    os.replace(tmp, out)


def box_down(a):
    """the engine's 2x2 box filter, each channel on its own (no premultiplication); one side may already be 1"""
    h, w = a.shape[:2]
    a = a.astype(np.uint16)
    a = a[0::2] + a[1::2] if h > 1 else a * 2
    a = a[:, 0::2] + a[:, 1::2] if w > 1 else a * 2
    return ((a + 2) // 4).astype(np.uint8)
