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
"""ff7renv.py -- an FF7 Rebirth time of day as a render-scene environment (maps/<map>.xenv in zz_<map>_env.pk3), and
the sky box and sun gltf2map takes for the gameplay BSP.

A Rebirth area's times of day are fixed states (the story switches them), each a sky map under
0001-WORLD/Layout/Lighting: its directional light (the sun), exponential height fogs and sky panorama. Costa del Sol:
SouthSeaSky_Morning, _Day, _Sunset (CostaSky_Sunset is the town's own sunset).

    python tools/levelport/ff7renv.py --extracted DIR --sky-map 0001-WORLD_SouthSeaSky_Day --map ff7r_costa --out DIR

The sun: a UE directional light shines along its rotator's X; the sun stands opposite (Quake axes: Y negated). Its
colour: LightColor (sRGB) x the blackbody at Temperature when bUseTemperature, as the game. The fog: the fogs'
albedo and density (UE per cm -> per metre). The panorama: the fog's Environment texture, graded (its export is
linear; --sky-gain) to <out>/sky_pano.png; its centre column faces Quake yaw 0 (the Day sky's drawn sun lies within 2 degrees of its light's direction so).
"""
import argparse
import collections
import json
import math
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import pk3write  # noqa: E402
from ff7rscene import UPCM, kelvin_rgb, srgb_linear  # noqa: E402


def exposure_volumes(recs, origin, ref_name):
    """The time of day's post-process volumes that set an exposure bias, as maps/<map>.xexp text (vk_scene_post.cpp):
    each an oriented box in game units (the volume's brush: UE's default 200 cm cube, scaled), its bias, priority and
    blend radius; ref the bias of the volume named ref_name (the outdoors r_sceneExposure was matched in)"""
    lines, ref = [], None
    for r in recs:
        if r.get('actorClass') != 'APostProcessVolume':
            continue
        ap = r.get('actorProperties') or {}
        st = ap.get('Settings') or {}
        if not st.get('bOverride_ExposureBias') or ap.get('bEnabled') is False:
            continue
        bias = float(st.get('ExposureBias', 0.0))
        if r['actor'] == ref_name:
            ref = bias
        if ap.get('bUnbound'):
            continue
        L, sc = r['location_cm'], r.get('scale') or [1.0, 1.0, 1.0]
        c = [(L[0] - origin[0]) * UPCM, (-L[1] - origin[1]) * UPCM, (L[2] - origin[2]) * UPCM]
        h = [100.0 * abs(float(v)) * UPCM for v in sc]
        yaw = -float((r.get('rotation_deg') or {}).get('Yaw', 0.0))      # (Y negated)
        lines.append('box %.1f %.1f %.1f %.1f %.1f %.1f %.2f %.2f %.1f %.1f  # %s' % (
            *c, *h, yaw, bias, float(ap.get('Priority', 0.0)), float(ap.get('BlendRadius', 100.0)) * UPCM, r['actor']))
    if ref is None:
        sys.exit('--exposure-ref %s: no such volume with an exposure bias' % ref_name)
    return ['xexp 1', 'ref %.2f' % ref] + lines


def reflection_atlas(prefix):
    """A UE reflection capture's BC6H cube (its levels' raw bytes: six faces each, +X -X +Y -Y +Z -Z in UE axes, each
    face's 4x4 blocks row by row) as one 2D BC6H DDS: the faces in a 4 x 2 atlas (+X -X +Y -Y over +Z -Z and two empty),
    its blocks moved as they are, levels down to a face of 4 texels (UE's are prefiltered by roughness: scene.frag's
    pleReflect picks the level)."""
    import struct
    levels = []
    lv = 0
    while Path('%s.mip%d.bin' % (prefix, lv)).exists():
        raw = Path('%s.mip%d.bin' % (prefix, lv)).read_bytes()
        bpf = len(raw) // 6                            # bytes a face
        bw = int(round(math.sqrt(bpf / 16)))           # blocks across a face
        if bw * bw * 16 * 6 != len(raw) or (levels and bw * 2 != levels[-1][0]):
            break
        rows = []
        for fy in range(2):
            for y in range(bw):
                for fx in range(4):
                    f = fy * 4 + fx
                    rows.append(raw[f * bpf + y * bw * 16:f * bpf + (y + 1) * bw * 16] if f < 6 else bytes(bw * 16))
        levels.append((bw, b''.join(rows)))
        lv += 1
        if bw == 1:
            break
    if not levels:
        sys.exit('--reflection: no levels at %s.mip0.bin' % prefix)
    w, h = levels[0][0] * 16, levels[0][0] * 8
    head = struct.pack('<4s7I44x', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000, h, w,
                       len(levels[0][1]), 0, len(levels))
    head += struct.pack('<2I4s5I', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    head += struct.pack('<5I', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    head += struct.pack('<5I', 95, 3, 0, 1, 0)        # DXGI_FORMAT_BC6H_UF16, 2D, one
    return head + b''.join(d for _, d in levels)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--extracted', required=True)
    ap.add_argument('--sky-map', required=True, help='the time of day: its sky map (Placements/<name>.json)')
    ap.add_argument('--map', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--sun', type=float, default=4.0, help='r_sceneSun')
    ap.add_argument('--ambient', type=float, default=0.6, help="r_sceneAmbient's scale (the fog albedo's hue)")
    ap.add_argument('--ambient-sat', type=float, default=0.4,
                    help="how much of the fog albedo's hue the ambient keeps (1: all; shadowed limestone reads near "
                         "neutral grey in the Day references, the fog's blue alone tinted it slate, look-3)")
    ap.add_argument('--ao', type=float, default=1.0, help="r_sceneAO: screen-space occlusion of the ambient (the "
                    "game's ambient is occluded; without it pier undersides and alleys lit like the open beach, look-4)")
    ap.add_argument('--fog', type=float, default=1.0, help="x the game's fog density")
    ap.add_argument('--exposure', type=float, default=1.0)
    ap.add_argument('--exposure-meter', help='r_sceneExposureMeter for this map: "key share" (the key of the eye adaptation, log2 '
                    'luminance at r_sceneExposure, and the share of the way it moves toward it; unset: the renderer default)')
    ap.add_argument('--sky-gain', type=float, default=1.5,
                    help="x the panorama's linear values before the sRGB encode (1.5: Day against the clear-sky "
                         "walkthrough frames)")
    ap.add_argument('--reflection', help="the sky map's baked reflection capture: the path prefix of its BC6H cube's "
                    "raw levels (<prefix>.mip<N>.bin, RebirthSceneExporter --dump-texture), packed as "
                    "maps/<map>_refl.dds (reflection_atlas)")
    ap.add_argument('--exposure-volumes', help="the time of day's map whose post-process volumes give rooms their own "
                    "exposure (Placements/<name>.json, e.g. 2110-COSTT_TimeSet_Day): maps/<map>.xexp; needs --scene")
    ap.add_argument('--exposure-ref', default='PostProcessVolume1_Town',
                    help='the volume whose bias r_sceneExposure stands for (the outdoors the look was matched in)')
    ap.add_argument('--scene', help="ff7rscene.py's build folder (scene.json: the origin) for --exposure-volumes")
    ap.add_argument('--exposure-volume-share', type=float, default=0.45,
                    help="r_sceneExposureVolumes: the share of a room's EV the volumes apply (the game's full 1 read "
                         "too bright against the ShaderInjector look; 0.45 the owner's pick, 2026-11-26)")
    ap.add_argument('--tone-hue', type=float, default=1.0,
                    help="r_sceneToneHue: the filmic toe's share kept to the colour's ratios (1: the per-channel toe drew "
                         "the hotel lobby's beige walls orange, saturation 0.56 -> 0.78; the beach is unchanged)")
    ap.add_argument('--shade', type=float, nargs=3, default=[1.0, 1.0, 1.0], metavar=('MICRO', 'SPECAO', 'SPECDIRECT'),
                    help="r_sceneMicroShadow, r_sceneSpecOcclusion, r_sceneSpecDirect: ShaderInjector's micro shadows, "
                         "specular occlusion and its sky reflection taken out where no direct light falls (0: off)")
    ap.add_argument('--contact', type=float, default=1.0,
                    help="r_sceneContactShadow: ShaderInjector's contact shadows on the sun (0: off; needs r_sceneAO)")
    ap.add_argument('--oren-nayar', type=float, default=1.0,
                    help="r_sceneOrenNayar: ShaderInjector's Oren-Nayar diffuse for the sun and lamps (0: Lambert)")
    ap.add_argument('--ao-multibounce', type=float, default=1.0,
                    help="r_sceneAOMultiBounce: ShaderInjector's GTAO multi-bounce on the ambient occlusion (0: off)")
    ap.add_argument('--tone-adjust', default='-0.45 1.15',
                    help="r_sceneToneAdjust \"EV gamma\": ShaderInjector's AdjustImage before the tone curve (\"0 1\": off)")
    ap.add_argument('--ple-radiance', type=float, default=1.0,
                    help="r_scenePleRadiance: the probes' reflection deconvolved, with their dominant light (0: off)")
    ap.add_argument('--camera-fill', type=float, default=1.0, help="r_sceneCameraFill: ShaderInjector's camera ambient light")
    ap.add_argument('--fog-far', default='60 1.5',
                    help="r_sceneFogFarDensity \"metres multiple\": ShaderInjector's far field fog (x2 there; Costa x1.5, owner 2026-11-27: x2 washed out the far hills) past UE's 60 m "
                         "volumetric fog distance (\"0 1\": off)")
    ap.add_argument('--bounce', type=float, default=1.0, help="r_sceneBounce: screen-space bounce light (0: off)")
    ap.add_argument('--ssr-glossy', default='1 0.35',
                    help="r_sceneSsrGlossy \"share roughness\": screen-space reflections on dry glossy surfaces (\"0\": off)")
    a = ap.parse_args()
    ex = Path(a.extracted)
    recs = json.loads((ex / 'Placements' / (a.sky_map + '.json')).read_text(encoding='utf-8'))
    sun = next(r for r in recs if (r.get('componentClass') or '').endswith('DirectionalLightComponent'))
    p = sun.get('properties') or {}
    lc = p.get('LightColor') or {'R': 255, 'G': 255, 'B': 255}
    col = srgb_linear([lc['R'], lc['G'], lc['B']])
    if p.get('bUseTemperature') and p.get('Temperature'):
        col = col * kelvin_rgb(float(p['Temperature']))
    col = col / max(col.max(), 1e-6)
    r = sun['rotation_deg']
    yaw = (-(r['Yaw'] + 180.0) + 180.0) % 360.0 - 180.0       # the light's opposite, Y negated
    elev = -r['Pitch']
    fogs = [x for x in recs if (x.get('componentClass') or '').endswith('ExponentialHeightFogComponent')]
    alb = collections.Counter()
    dens, pano = [], None
    for f in fogs:
        ctx = (f.get('properties') or {}).get('ContextA') or {}
        if ctx.get('Albedo'):
            alb[(ctx['Albedo']['R'], ctx['Albedo']['G'], ctx['Albedo']['B'])] += 1
        if ctx.get('DensityBasis') is not None:
            dens.append(float(ctx['DensityBasis']))
        env = ((f.get('properties') or {}).get('Environment') or {}).get('Texture') or {}
        pano = pano or env.get('ObjectPath')
    fog_rgb = alb.most_common(1)[0][0] if alb else (180, 185, 190)
    fog_d = (sorted(dens)[len(dens) // 2] * 100.0 if dens else 0.0005) * a.fog
    amb = srgb_linear(fog_rgb)
    amb = amb / max(amb.max(), 1e-6)
    amb = (1.0 - a.ambient_sat) + a.ambient_sat * amb        # (toward grey: the sand's bounce, not the sky alone)
    amb = amb / amb.max() * a.ambient
    lines = ['xenv 1', '# %s (tools/levelport/ff7renv.py)' % a.sky_map, 'time day',
             'sun %.3f %.3f %.3f 100 %.2f %.2f' % (col[0], col[1], col[2], yaw, elev),
             'r_sceneSun %.3f' % a.sun,
             'r_sceneAmbient %.4f %.4f %.4f' % tuple(amb),
             'r_sceneFog %d %d %d' % fog_rgb,
             'r_sceneFogDensity %.5f' % fog_d,
             'r_sceneExposure %.3f' % a.exposure,
             'r_sceneAO %.2f' % a.ao,
             'r_sceneAORadius 3.00',
             'r_sceneShadowFar 60000',
             'r_sceneCharSun 1',
             'r_sceneWet 0',
             'r_sceneToneHue %.2f' % a.tone_hue, 'r_sceneMicroShadow %.2f' % a.shade[0],
             'r_sceneSpecOcclusion %.2f' % a.shade[1], 'r_sceneSpecDirect %.2f' % a.shade[2], 'r_sceneContactShadow %.2f' % a.contact, 'r_sceneOrenNayar %.2f' % a.oren_nayar,
             'r_sceneAOMultiBounce %.2f' % a.ao_multibounce, 'r_sceneToneAdjust %s' % a.tone_adjust,
             'r_scenePleRadiance %.2f' % a.ple_radiance, 'r_sceneCameraFill %.2f' % a.camera_fill,
             'r_sceneFogFarDensity %s' % a.fog_far, 'r_sceneBounce %.2f' % a.bounce, 'r_sceneSsrGlossy %s' % a.ssr_glossy] + (['r_sceneExposureMeter %s' % a.exposure_meter] if a.exposure_meter else [])         + (['r_sceneExposureVolumes %.2f' % a.exposure_volume_share] if a.exposure_volumes else [])
    out = Path(a.out)
    (out / 'maps').mkdir(parents=True, exist_ok=True)
    xenv = out / 'maps' / (a.map + '.xenv')
    xenv.write_text('\n'.join(lines) + '\n', encoding='ascii', newline='\n')
    pk3 = out / ('zz_%s_env.pk3' % a.map)
    refl = reflection_atlas(a.reflection) if a.reflection else None
    with pk3write.writing(str(pk3)) as z:
        z.write(xenv, 'maps/%s.xenv' % a.map)
        if refl:
            z.writestr('maps/%s_refl.dds' % a.map, refl)
        if a.exposure_volumes:
            origin = json.loads((Path(a.scene) / 'scene.json').read_text(encoding='utf-8'))['origin_cm']
            vrecs = json.loads((ex / 'Placements' / (a.exposure_volumes + '.json')).read_text(encoding='utf-8'))
            xexp = exposure_volumes(vrecs, origin, a.exposure_ref)
            z.writestr('maps/%s.xexp' % a.map, '\n'.join(xexp) + '\n')
            print('exposure volumes: %d, ref %s' % (len(xexp) - 2, xexp[1]))
    pano_file = None
    if pano:
        rel = pano.rsplit('.', 1)[0].replace('/Game/', 'End/Content/', 1)
        q = ex / 'Sky' / (rel + '.png')
        if q.exists():
            # the export holds the HDR panorama's linear values in 8 bits (Day's zenith 2 14 51): drawn as sRGB it
            # was navy. Linear x --sky-gain, sRGB-encoded, at 8192 x 4096 (a 2048 sky box face's own density)
            import numpy as np
            from PIL import Image
            Image.MAX_IMAGE_PIXELS = None
            with Image.open(q) as im:
                L = np.asarray(im.convert('RGB').resize((8192, 4096), Image.BOX), np.float64) / 255.0 * a.sky_gain
            L = np.clip(L, 0, 1)
            S = np.where(L <= 0.0031308, 12.92 * L, 1.055 * L ** (1 / 2.4) - 0.055)
            pano_file = str(out / 'sky_pano.png')
            Image.fromarray(np.uint8(np.round(S * 255))).save(pano_file)
    srgb = [round(255 * c ** (1 / 2.2)) / 255 for c in col]
    rec = {'sky_map': a.sky_map, 'sun_yaw': yaw, 'sun_elevation': elev, 'sun_rgb_linear': col.tolist(),
           'fog_rgb': fog_rgb, 'fog_density_per_m': fog_d, 'panorama': pano_file, 'xenv': lines,
           'gltf2map': ['--sun', '%.3f %.3f %.3f 120 %.1f %.1f' % (srgb[0], srgb[1], srgb[2], yaw % 360.0, elev)] +
                       (['--sky-pano', pano_file, '--sky-pano-yaw', '0'] if pano_file else [])}
    (out / 'env.json').write_text(json.dumps(rec, indent=1), encoding='utf-8')
    print('FF7RENV', pk3)
    print('\n'.join(lines))
    print('gltf2map', json.dumps(rec['gltf2map']))


if __name__ == '__main__':
    main()
