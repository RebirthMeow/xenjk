"""w3env.py -- a Witcher 3 world's daylight as a render-scene environment (maps/<map>.xenv), Beauclair.

Runs in headless Blender with the Witcher3-Blender-Tools add-on (its CR2W environment readers):

    blender -b --python tools/levelport/w3env.py -- --out DIR --map w3_castle [--hour 14]
        [--world dlc/bob/data/levels/bob/bob.w2w] [--sun 4] [--ambient 0.8] [--sky 0.6] [--exposure 1.0] [--fog 1]

Defaults chosen by looking at the castle: sun 4, ambient 0.8, sky 0.6.

Reads the world's environment parameters (its default environment definition, the sun's trajectory) and evaluates
the definition's curves at --hour, then writes:

    DIR/maps/<map>.xenv   (vk_scene.cpp Scene_LoadEnv: time day, the sun's colour and direction, the look cvars)
    DIR/zz_<map>_env.pk3  that file packed, to stage beside the map and scene packs
    DIR/env.json          what was read and how it was mapped

Mapping (the game's units are not this renderer's; the scales are the calibration, chosen by looking):
- the sun's direction: the trajectory's light direction (the game lights from higher than it draws its sun), as
  the q3gl2_sun order's yaw and elevation; its colour: m_globalLight.sunColor's hue. Strength: --sun (r_sceneSun).
- ambient: m_globalLight.envProbeBaseLightingAmbient.colorAmbient's hue x --ambient (r_sceneAmbient, linear).
- the analytic sky (r_sceneSky 1): m_sky.skyColor / skyColorHorizon, linear, x --sky (the scene's units); by default
  the game's own ratio of the sky to the sun: --sun x m_sky.globalSkyBrightness / m_globalLight.sunColor's scale
  (6 x 7.62 / 182 = 0.25; the hand-set 0.6 drew it pale). Clouds: the game's are particle systems (the
  weather table's WT_Clear: bob_mid_clouds_small_1.w2p), not converted; --clouds turns on the renderer's own cumulus as
  a stand-in. Under the horizon, where the map's ground ends: --ground (sRGB 0-255 at the sky's scale; the valley's
  hazed blue-green in the wiki's palace shot; clouds 0.5 at 3 km, curve 2), a stand-in until the valley's terrain is converted.
- exposure: m_toneMapping.exposureScale x --exposure (r_sceneExposure).
- haze: m_globalFog.fogColorFront / fogColorMiddle averaged (r_sceneFog), m_globalFog.fogDensity x --fog.
Values are written without quotes: Scene_LoadEnv keeps a value's characters as they are.
"""
import argparse
import importlib
import json
import math
import os
import sys

PKG = 'bl_ext.user_default.witcher3_tools'
BS = chr(92)


def srgb_to_linear(c):
    c = max(0.0, min(1.0, c))
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hue(rgb255):
    """A colour's channels over its brightest (0..1, gamma as stored), linearised."""
    m = max(max(rgb255[:3]), 1e-6)
    return [srgb_to_linear(c / m) for c in rgb255[:3]]


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--map', default='w3_castle')
    ap.add_argument('--hour', type=float, default=14.0)
    ap.add_argument('--world', default='dlc/bob/data/levels/bob/bob.w2w')
    ap.add_argument('--sun', type=float, default=4.0)
    ap.add_argument('--ambient', type=float, default=0.8)
    ap.add_argument('--sky', type=float, default=None, help="the sky's scale (default: the game's sky-to-sun ratio)")
    ap.add_argument('--sky-curve', type=float, default=2.0, help="r_sceneSkyCurve (the horizon colour's reach)")
    ap.add_argument('--clouds', type=float, default=0.5, help="r_sceneClouds: the renderer's cumulus, a stand-in (0 off)")
    ap.add_argument('--cloud-scale', type=float, default=3.0, help="r_sceneCloudScale (km)")
    ap.add_argument('--ground', default='haze:0.85', help="the land under the horizon, sRGB 0-255 at the sky's scale ('' none)")
    ap.add_argument('--exposure', type=float, default=1.0)
    ap.add_argument('--fog', type=float, default=1.0, help="times the game's fog density")
    ap.add_argument('--grade', action='store_true', help="the game's grade (w3grade.py's .xlut) does the exposure")
    ap.add_argument('--grade-exposure', type=float, default=8.0,
                    help="the light handed to the game's grade (this renderer's light units to the game's)")
    ap.add_argument('--ao', type=float, default=1.0, help="r_sceneAO: screen-space occlusion on the ambient (0 off)")
    ap.add_argument('--ao-radius', type=float, default=3.0, help="r_sceneAORadius (m)")
    ap.add_argument('--foliage', type=float, default=0.0,
                    help="r_sceneFoliage: the SpeedTree leaves' canopy shading (0: the renderer's default, off; "
                         "Beauclair uses 0.7)")
    ap.add_argument('--shadow-far', type=float, default=0.0,
                    help="r_sceneShadowFar: the far sun cascade's reach (units; 0: r_shadowCascadeZFar's 4096, ~110 m). "
                         "A city seen from the air needs more: its shadows ended in a line that moved with the eye")
    a = ap.parse_args(argv)

    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    env = importlib.import_module(PKG + '.CR2W.dc_environment')
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (w3patches.py)
    import pk3write
    import w3patches
    w3patches.apply()

    world = env.load_world_environment(mat.materialize_repo_path(a.world.replace('/', BS), source_game='w3'))
    definition = env.load_environment_definition(
        mat.materialize_repo_path(world.environment_definition, source_game='w3'))
    t = a.hour * 3600.0 / 86400.0

    def curve(name, default):
        c = definition.curves.get(name)
        if c is None or env.curve_is_placeholder(c):
            return list(default)
        return [float(x) for x in c.evaluate(t)]

    light = [float(x) for x in world.trajectory.light_direction(a.hour * 3600.0)]
    sun_vis = [float(x) for x in world.trajectory.sun_direction(a.hour * 3600.0)]
    yaw = math.degrees(math.atan2(light[1], light[0])) % 360.0
    elev = math.degrees(math.asin(max(-1.0, min(1.0, light[2] / (math.sqrt(sum(x * x for x in light)) or 1.0)))))
    sun_col = curve('m_globalLight.sunColor', (255, 255, 255, 1))
    amb = curve('m_globalLight.envProbeBaseLightingAmbient.colorAmbient', (255, 255, 255, 1))
    sky_top = curve('m_sky.skyColor', (108, 158, 230, 1))
    sky_hor = curve('m_sky.skyColorHorizon', (230, 186, 158, 1))
    expo = curve('m_toneMapping.exposureScale', (0, 0, 0, 1))[3]
    fog_front = [max(0.0, min(255.0, c)) for c in curve('m_globalFog.fogColorFront', (180, 185, 190, 1))]
    fog_mid = [max(0.0, min(255.0, c)) for c in curve('m_globalFog.fogColorMiddle', (180, 185, 190, 1))]
    fog_density = curve('m_globalFog.fogDensity', (0, 0, 0, 0.0006))[3]

    sc = hue(sun_col)
    ambient = [c * a.ambient for c in hue(amb)]
    if a.sky is None:
        a.sky = a.sun * curve('m_sky.globalSkyBrightness', (0, 0, 0, 7.6))[3] / max(sun_col[3], 1e-3)
    zen = [srgb_to_linear(c / 255.0) * a.sky for c in sky_top[:3]]
    hor = [srgb_to_linear(c / 255.0) * a.sky for c in sky_hor[:3]]
    if a.ground.startswith('haze:'):     # the haze's own colour x the factor (renderer: a negative red)
        ground = [-float(a.ground.split(':')[1]), 0.0, 0.0]
    else:
        ground = [srgb_to_linear(float(c) / 255.0) * a.sky for c in a.ground.split()] if a.ground.strip() else [0.0] * 3
    lines = ['xenv 1',
             '# %s at %.1f h (tools/levelport/w3env.py)' % (world.environment_definition.replace(BS, '/'), a.hour),
             'time day',
             'sun %.3f %.3f %.3f 100 %.2f %.2f' % (sc[0], sc[1], sc[2], yaw, elev),
             'r_sceneSun %.3f' % a.sun,
             # (no quotes: Scene_LoadEnv keeps a value's characters as they are; quoted vectors read as 0)
             'r_sceneAmbient %.4f %.4f %.4f' % tuple(ambient),
             'r_sceneSky 1',
             'r_sceneSkyZenith %.4f %.4f %.4f' % tuple(zen),
             'r_sceneSkyHorizon %.4f %.4f %.4f' % tuple(hor),
             'r_sceneSkyGround %.4f %.4f %.4f' % tuple(ground),
             'r_sceneSkyCurve %.2f' % a.sky_curve,
             'r_sceneClouds %.2f' % a.clouds, 'r_sceneCloudScale %.2f' % a.cloud_scale,
             # the haze: the game's global fog, its colour between the front (toward the sun) and middle colours
             # (sRGB 0-255, as r_sceneFog takes it) and its density (per metre); the renderer's default is Night
             # City's brown 0.005
             'r_sceneFog %d %d %d' % tuple(round((f + m) / 2) for f, m in zip(fog_front[:3], fog_mid[:3])),
             'r_sceneFogDensity %.5f' % (fog_density * a.fog),
             # with the game's display transform as the grade (w3grade.py, maps/<map>.xlut: its exposure, tone curve and
             # balance map), the renderer's own exposures are 1 so the light is scaled once
             ('r_sceneExposure %.3f' % a.exposure) if a.grade else ('r_sceneExposure %.3f' % (expo * a.exposure)),
             ] + (['r_sceneGrade 1', 'r_sceneGradeExposure %.3f' % a.grade_exposure] if a.grade else []) + [
             'r_sceneCharSun 1',
             # a dry day: no wet ground (the renderer's default wets up-facing surfaces, Night City's rain)
             'r_sceneWet 0',
             # the game's SSAO (HBAO+ on PC) on the ambient light; the renderer's own pass (r_sceneAO)
             'r_sceneAO %.2f' % a.ao, 'r_sceneAORadius %.2f' % a.ao_radius] + (
                 ['r_sceneShadowFar %.0f' % a.shadow_far] if a.shadow_far > 0 else []) + (
                 ['r_sceneFoliage %.2f' % a.foliage] if a.foliage > 0 else [])
    os.makedirs(os.path.join(a.out, 'maps'), exist_ok=True)
    xenv = os.path.join(a.out, 'maps', a.map + '.xenv')
    with open(xenv, 'w', encoding='ascii', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    pk3 = os.path.join(a.out, 'zz_%s_env.pk3' % a.map)
    with pk3write.writing(pk3) as z:
        z.write(xenv, 'maps/%s.xenv' % a.map)
    rec = {'world': world.source_path, 'definition': world.environment_definition, 'hour': a.hour,
           'light_direction': light, 'sun_direction_drawn': sun_vis, 'yaw': yaw, 'elevation': elev,
           'sunColor': sun_col, 'colorAmbient': amb, 'skyColor': sky_top, 'skyColorHorizon': sky_hor,
           'exposureScale': expo, 'scales': {'sun': a.sun, 'ambient': a.ambient, 'sky': a.sky, 'exposure': a.exposure},
           'xenv': lines}
    with open(os.path.join(a.out, 'env.json'), 'w') as f:
        json.dump(rec, f, indent=1)
    print('W3ENV', pk3)
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
