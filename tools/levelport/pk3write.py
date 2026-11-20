# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
"""Writing level-port packs.

writing(path): a pack written under <name>.tmp and renamed to its name once closed, so a stopped run never leaves a
short pack under the name later steps trust (the engine loads only *.pk3).

ShardWriter: a scene's packs as patch_water.py and tag_interiors.py write them: the first, zz_<map>_scene.pk3, then
zz_<map>_scene2.pk3, 3, ... once the next stored shard would pass LIMIT. link_unchanged: the image packs and the
scene's JSON beside them, hard-linked from the source kit's folder."""
import contextlib
import os
import zipfile

LIMIT = 1_900_000_000          # stored shards per pack (zipfile writes no offset past 2 GB without zip64)


@contextlib.contextmanager
def writing(path, *args, **kw):
    """zipfile.ZipFile(path, 'w', *args, **kw) written as <path>.tmp and renamed when the block ends without an
    exception (the .tmp is left otherwise)"""
    path = os.fspath(path)
    tmp = path + '.tmp'
    z = zipfile.ZipFile(tmp, 'w', *args, **kw)
    try:
        yield z
    finally:
        z.close()
    os.replace(tmp, path)


class ShardWriter:
    """A scene's packs in a folder: first the main pack, then one more whenever a stored shard would pass LIMIT"""

    def __init__(self, folder, mapname):
        self.folder, self.map = folder, mapname
        self.packs = [zipfile.ZipFile(os.path.join(folder, 'zz_%s_scene.pk3' % mapname), 'w', allowZip64=False)]
        self.w = self.packs[0]

    @property
    def first(self):
        return self.packs[0]

    def stored(self, info, data):
        """A shard (stored, its zip info's name and time) in the current pack, or a new one when it would pass LIMIT"""
        if self.w.fp.tell() + len(data) + 4096 > LIMIT:
            self.w = zipfile.ZipFile(os.path.join(self.folder, 'zz_%s_scene%d.pk3' % (self.map, len(self.packs) + 1)),
                                     'w', allowZip64=False)
            self.packs.append(self.w)
        self.w.writestr(zipfile.ZipInfo(info.filename, info.date_time), data, compress_type=zipfile.ZIP_STORED)

    def close(self):
        for z in self.packs:
            z.close()


def link_unchanged(src, packs, folder):
    """The source's image packs (*_img*) and its materials.json / ad_lights.json hard-linked into folder"""
    for p in packs:
        if '_img' in p.name:
            os.link(p, os.path.join(folder, p.name))
    for f in ('materials.json', 'ad_lights.json'):
        if (src / f).exists():
            os.link(src / f, os.path.join(folder, f))
