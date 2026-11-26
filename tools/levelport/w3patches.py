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

apply() prints W3PATCHES_APPLIED.

use_w3apb(force) is applied separately (w3flags.py): APEX cloth without NVIDIA ParamTool. The add-on converts each
cloth .apb to .apx with ParamTool (external_addon_tools.ensure_apx_from_apb, io_mesh_apx's `apex_sdk_cli`); the
replacement writes the same .apx with w3apb.py when that tool is missing (or always, with force), and keeps the
add-on's result shape, so the rest of its import is unchanged. Prints W3PATCHES_W3APB."""
import importlib
import os
import struct
import sys

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

    # 3) $W3_OVERRIDE: depot folders (';'-separated) whose files win over the uncook, read only -- a mod's files
    #    unpacked from its bundles (e.g. Filterless Toussaint's sunny_bob_v7.env), nothing in the uncook changed
    roots = [r for r in os.environ.get('W3_OVERRIDE', '').split(';') if r.strip()]
    if roots:
        cb = importlib.import_module(PKG + ".CR2W.common_blender")
        cb.set_repo_override_roots(roots, read_only=True)
        print("W3PATCHES_OVERRIDE", roots)
    print("W3PATCHES_APPLIED")


def use_w3apb(force=False):
    """3) Cloth .apb -> .apx with w3apb.py instead of ParamTool (when ParamTool is not configured, or force)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import w3apb

    eat = importlib.import_module(PKG + ".external_addon_tools")
    orig = eat.ensure_apx_from_apb
    used = {"w3apb": 0, "paramtool": 0}

    def ensure_apx_from_apb(context, apb_path, overwrite=False):
        status = eat.get_apx_addon_status(context)
        if status["sdk_ready"] and not force:
            used["paramtool"] += 1
            return orig(context, apb_path, overwrite)
        apb_path = os.path.normpath(apb_path or "")
        apx_path = os.path.splitext(apb_path)[0] + ".apx" if apb_path else ""
        result = {"status": "invalid_input", "apb_path": apb_path, "apx_path": apx_path, "returncode": None,
                  "message": ""}
        if not apb_path or os.path.splitext(apb_path)[1].lower() != ".apb":
            result["message"] = "Input is not an .apb file."
            return result
        if not os.path.isfile(apb_path):
            result["status"] = "missing_apb"
            result["message"] = "APB file does not exist."
            return result
        if not status["enabled"]:
            result["status"] = "apx_addon_disabled"
            result["message"] = "io_mesh_apx addon is not enabled."
            return result
        existed = os.path.isfile(apx_path)
        if existed and not overwrite:
            result["status"] = "exists"
            result["message"] = "APX already exists."
            return result
        try:
            with open(apb_path, "rb") as fh:
                data = fh.read()
            o = data.find(struct.pack(">I", w3apb.MAGIC))
            w3apb.to_apx(data[max(o, 0):], apx_path)
            used["w3apb"] += 1
            result["returncode"] = 0
            result["status"] = "updated" if existed else "converted"
        except Exception as exc:  # the add-on reports and skips the cloth, as for a ParamTool failure
            result["status"] = "failed"
            result["message"] = "w3apb: %s" % exc
        return result

    eat.ensure_apx_from_apb = ensure_apx_from_apb
    for mod in (".ui.ui_file_browser", ".ui.ui_cache_export"):
        try:
            m = importlib.import_module(PKG + mod)
        except Exception:
            continue
        if getattr(m, "ensure_apx_from_apb", None) is orig:
            m.ensure_apx_from_apb = ensure_apx_from_apb
    print("W3PATCHES_W3APB force=%s" % bool(force))
    return used
