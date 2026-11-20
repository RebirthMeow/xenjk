"""pack_order.py -- the engine's pk3 order for the pack and map tools.

qcommon/files.cpp paksort: packs whose name starts with dl_ (any case) come after the others; otherwise FS_PathCmp,
which compares upper-cased characters and reads a backslash or colon as a slash. A later pack is searched first, so it
wins. Upper-casing is what puts _ after the letters (zz_x.pk3 after zzz.pk3), which a lower-cased sort gets the other
way round."""


def pack_sort_key(name):
    """sort key for a pk3 file name (or a Path: its .name), in the engine's load order"""
    n = getattr(name, 'name', name)
    return (n[:3].lower() == 'dl_', n.upper().replace('\\', '/').replace(':', '/'))
