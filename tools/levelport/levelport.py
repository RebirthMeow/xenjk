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
"""Shared levelport conventions: units, axes, node roles and Quake 3 surfaceparm names.

glTF side: metres, +Y up, -Z forward (right-handed). Map side: Quake units, +Z up.
    gltf = (x, z, -y) / UNITS_PER_METRE      q3 = (gx, -gz, gy) * UNITS_PER_METRE
UNITS_PER_METRE is 37.5, the value the cloth pipeline uses for Blender (a 64-unit player is 1.71 m).

Node roles live in node extras under "lp" (Blender: object custom property "lp", exported with
Include > Custom Properties). See README.md for the full convention.
"""
import numpy as np

UNITS_PER_METRE = 37.5

ROLE_RENDER, ROLE_COLLISION, ROLE_PATCH, ROLE_ENTITY, ROLE_IGNORE = 'render', 'collision', 'patch', 'entity', 'ignore'

# surfaceparm names q3map2 -game ja understands and that matter after compile. Content parms decide whether a
# collision brush exists at all; surface parms ride on its sides.
CONTENT_PARMS = ('lava', 'slime', 'water', 'fog', 'playerclip', 'monsterclip', 'botclip', 'shotclip', 'trigger',
                 'nodrop', 'detail', 'trans', 'nonsolid')
SURFACE_PARMS = ('sky', 'slick', 'nodamage', 'noimpact', 'nomarks', 'nosteps', 'nodlight', 'metalsteps')
GAMEPLAY_CONTENTS = ('solid', 'lava', 'slime', 'water', 'fog', 'playerclip', 'monsterclip', 'botclip', 'shotclip',
                     'trigger', 'nodrop')


def q3_to_gltf(p, upm=UNITS_PER_METRE):
    p = np.asarray(p, dtype=np.float64)
    return np.stack([p[..., 0], p[..., 2], -p[..., 1]], axis=-1) / upm


def gltf_to_q3(g, upm=UNITS_PER_METRE):
    g = np.asarray(g, dtype=np.float64)
    return np.stack([g[..., 0], -g[..., 2], g[..., 1]], axis=-1) * upm


def dir_q3_to_gltf(n):
    n = np.asarray(n, dtype=np.float64)
    return np.stack([n[..., 0], n[..., 2], -n[..., 1]], axis=-1)


def dir_gltf_to_q3(n):
    n = np.asarray(n, dtype=np.float64)
    return np.stack([n[..., 0], -n[..., 2], n[..., 1]], axis=-1)
