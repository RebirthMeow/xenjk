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
"""Where the level port's tools find things: one place for every location, each from an environment variable.

    REPO        the repository (from this file)
    RESEARCH    work and output root: $XENJK_RESEARCH, else <repo>/.codex-research
    LEVELPORT   RESEARCH/levelport: builds, work folders, caches, looks
    GAMEDATA    the game directory: $XENJK_GAMEDATA, else <repo>/game_directory/Jedi Academy/GameData

Tools outside the repository have no default; each is read when a tool needs it, and a missing one exits naming its
variable:

    Q3MAP2            the q3map2 build with tools/levelport/q3map2's patches (q3map2/README.md)
    Q3MAP2_CHUNKS     the build with exact_planes, brush_cook and brush_bin (gltf2map --exact-planes / --cook,
                      cp77_collision's chunks)
    WOLVENKIT_CLI     WolvenKit.CLI.exe;  WOLVENKIT_DOTNET  the .NET runtime folder it runs on
    CP77_GAME         the Cyberpunk 2077 install folder
    BLENDER           blender.exe
    PWSH              PowerShell 7 (default: pwsh on PATH)
    XENJK_LIBZSTD     libzstd's shared library (colpack.py)
"""
import os
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RESEARCH = Path(os.environ.get('XENJK_RESEARCH') or REPO / '.codex-research')
LEVELPORT = RESEARCH / 'levelport'
GAMEDATA = Path(os.environ.get('XENJK_GAMEDATA') or REPO / 'game_directory' / 'Jedi Academy' / 'GameData')

HELP = {
    'Q3MAP2': 'the patched q3map2.exe (tools/levelport/q3map2/README.md)',
    'Q3MAP2_CHUNKS': 'the q3map2.exe with exact_planes, brush_cook and brush_bin (tools/levelport/q3map2/README.md)',
    'WOLVENKIT_CLI': 'WolvenKit.CLI.exe',
    'WOLVENKIT_DOTNET': 'the .NET runtime folder WolvenKit.CLI runs on',
    'CP77_GAME': 'the Cyberpunk 2077 install folder',
    'BLENDER': 'blender.exe',
    'XENJK_LIBZSTD': "libzstd's shared library",
    'XENJK_X64': "the x64 Release build folder (xenjk.x86_64.exe and its modules) levelcheck's client runs",
}


def env_path(var):
    """$var as a Path, or None when it is not set"""
    v = os.environ.get(var)
    return Path(v) if v else None


def need(var, exists=True):
    """$var as a Path; exits naming the variable when it is not set (or, with `exists`, names nothing on disk)"""
    p = env_path(var)
    if p is None:
        sys.exit('set $%s: %s' % (var, HELP.get(var, 'a path')))
    if exists and not p.exists():
        sys.exit('$%s is %s, which does not exist (%s)' % (var, p, HELP.get(var, 'a path')))
    return p


def pwsh():
    """PowerShell 7: $PWSH, else pwsh on PATH"""
    p = os.environ.get('PWSH') or shutil.which('pwsh')
    if not p:
        sys.exit('set $PWSH: PowerShell 7 (pwsh.exe), or put pwsh on PATH')
    return p
