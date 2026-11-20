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
"""Where the Witcher 3 tools find what is not in the repository. Every location comes from an argument or an
environment variable; there is no machine default, and a missing one ends the run with the variable's name.

    W3_GAME      the game's folder (the one holding bin/ and content/): w3bundle.py, w3emitter.py.
                 W3_ROOT is read when W3_GAME is not set (its older name).
    W3_UNCOOK    the Witcher3-Blender-Tools add-on's uncook folder (its "uncook path" preference): the .srt files and
                 their JSON (w3trees.py), the flow and particle textures (w3scene.py, w3fx.py), w3flags.py's pole
                 texture. When unset: <W3_RESEARCH>/uncook.
    W3_RESEARCH  a research folder whose uncook/ is the add-on's uncook folder (the layout the guide describes);
                 the tools' --w3 argument overrides it.
    W3_DOBOZ     the add-on's Doboz decompressor, CR2W/witcher_cache/Bundles/native/Doboz.dll under the installed
                 add-on: w3bundle.py, for the bundle files compressed with doboz."""
import os


def _need(name, value, what):
    if not value:
        raise SystemExit('%s: set %s (%s)' % (os.path.basename(__file__), name, what))
    return value


def game(arg=None):
    """The game's folder: arg, else $W3_GAME, else $W3_ROOT"""
    return _need('W3_GAME', arg or os.environ.get('W3_GAME') or os.environ.get('W3_ROOT'),
                 "the Witcher 3 game folder")


def research(arg=None, required=True):
    """The research folder: arg (a tool's --w3), else $W3_RESEARCH; None when not required and unset"""
    v = arg or os.environ.get('W3_RESEARCH')
    return _need('W3_RESEARCH', v, 'the W3 research folder, or pass --w3') if required else v


def uncook(research_arg=None):
    """The add-on's uncook folder: <research_arg>/uncook when a tool was given --w3, else $W3_UNCOOK, else
    <$W3_RESEARCH>/uncook"""
    if research_arg:
        return os.path.join(research_arg, 'uncook')
    v = os.environ.get('W3_UNCOOK')
    if v:
        return v
    r = os.environ.get('W3_RESEARCH')
    return _need('W3_UNCOOK', os.path.join(r, 'uncook') if r else None, "the add-on's uncook folder")


def doboz(arg=None):
    """The Doboz decompressor DLL: arg, else $W3_DOBOZ"""
    return _need('W3_DOBOZ', arg or os.environ.get('W3_DOBOZ'),
                 "the add-on's CR2W/witcher_cache/Bundles/native/Doboz.dll")
