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
"""Runtime patches to the Witcher3-Blender-Tools add-on (1.1.0), applied by the W3 Blender scripts before they use
it; the add-on's own files are not changed.

1. SpeedTree: a CR2W-wrapped .srt is unwrapped to its raw `SRT 07` blob before the add-on's JSON converter
   (ui.ui_file_browser._srt_json_from_file) sees it. The wrapped original is kept beside it as <name>.srt.cr2w, and
   the converter's <name>.srt.json from the wrapped file is removed so the converter writes it again.
2. Door entities: the opaque per-instance runtime state (`BufferV1`) is dropped from an entity's overrides instead
   of aborting the whole layer (importers.import_blender_fun._entity_instance_override_shape_error).

apply() prints W3PATCHES_APPLIED."""
import importlib
import os
import struct

PKG = "bl_ext.user_default.witcher3_tools"


def _unwrap_srt(path):
    """Replace a CR2W-wrapped .srt with its raw SpeedTree blob (original kept as .srt.cr2w)."""
    try:
        with open(path, "rb") as fh:
            b = fh.read()
    except OSError:
        return
    if not b.startswith(b"CR2W"):
        return
    o = b.find(b"SRT 07")
    if o < 4:
        return
    n = struct.unpack("<I", b[o - 4:o])[0]
    if n <= 0 or o + n > len(b):
        return
    os.replace(path, path + ".cr2w")
    with open(path, "wb") as fh:
        fh.write(b[o:o + n])
    if os.path.exists(path + ".json"):
        os.remove(path + ".json")


def apply():
    # 1) SpeedTree: unwrap CR2W .srt before the JSON converter sees it
    fb = importlib.import_module(PKG + ".ui.ui_file_browser")
    orig_json = fb._srt_json_from_file

    def srt_json_from_file(abs_file_path, output_dir=""):
        p = fb.win_unprefix_path(abs_file_path or "")
        if p.lower().endswith(".srt") and os.path.exists(p):
            _unwrap_srt(p)
        return orig_json(abs_file_path, output_dir)

    fb._srt_json_from_file = srt_json_from_file

    # 2) Door entities: drop opaque per-instance runtime state (BufferV1) instead of aborting the whole layer
    ibf = importlib.import_module(PKG + ".importers.import_blender_fun")
    orig_err = ibf._entity_instance_override_shape_error

    def shape_err(overrides):
        if isinstance(overrides, dict) and overrides.get("BufferV1"):
            overrides.pop("BufferV1", None)
        return orig_err(overrides)

    ibf._entity_instance_override_shape_error = shape_err
    print("W3PATCHES_APPLIED")
