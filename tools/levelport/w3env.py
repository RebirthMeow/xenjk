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
    ap.add_argument('--hours', type=float, default=0.0,
                    help="the day/night cycle: keyframes every this many hours round the clock (0: none, the one "
                         "--hour). Each is the game's curves at that hour, scaled by the calibration at --hour")
    ap.add_argument('--lamp-elevation', type=float, default=4.0,
                    help="the cycle's city lamps are lit while the drawn sun stands lower than this (degrees)")
    ap.add_argument('--side-elevation', type=float, default=12.0,
                    help="the cycle's sun-side horizon colour (sunColorHorizon) shows as the drawn sun falls under this")
    ap.add_argument('--lamp-fade', type=float, default=0.5, help="hours a city lamp takes to light or go out")
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

    def curve_at(name, default, hour):
        c = definition.curves.get(name)
        if c is None or env.curve_is_placeholder(c):
            return list(default)
        return [float(x) for x in c.evaluate((hour % 24.0) * 3600.0 / 86400.0)]

    def curve(name, default):
        return curve_at(name, default, a.hour)

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
    sky_given = a.sky is not None
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
    if a.hours > 0:
        lines += cycle_lines(a, world, curve_at, sky_given)
    os.makedirs(os.path.join(a.out, 'maps'), exist_ok=True)
    xenv = os.path.join(a.out, 'maps', a.map + '.xenv')
    with open(xenv, 'w', encoding='ascii', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    pk3 = os.path.join(a.out, 'zz_%s_env.pk3' % a.map)
    with pk3write.writing(pk3) as z:
        z.write(xenv, 'maps/%s.xenv' % a.map)
        xlt = os.path.join(a.out, 'maps', a.map + '.xlt')
        if os.path.isfile(xlt):     # (w3lighttime.py's: the city lamps the cycle lights at night)
            z.write(xlt, 'maps/%s.xlt' % a.map)
    rec = {'world': world.source_path, 'definition': world.environment_definition, 'hour': a.hour,
           'light_direction': light, 'sun_direction_drawn': sun_vis, 'yaw': yaw, 'elevation': elev,
           'sunColor': sun_col, 'colorAmbient': amb, 'skyColor': sky_top, 'skyColorHorizon': sky_hor,
           'exposureScale': expo, 'scales': {'sun': a.sun, 'ambient': a.ambient, 'sky': a.sky, 'exposure': a.exposure},
           'xenv': lines}
    with open(os.path.join(a.out, 'env.json'), 'w') as f:
        json.dump(rec, f, indent=1)
    print('W3ENV', pk3)
    print('\n'.join(lines))


def cycle_lines(a, world, curve_at, sky_given):
    """The day/night cycle's lines (vk_scene.cpp Scene_LoadEnv: `cycle`, `lamps`, then `hour` blocks of the values
    the clock blends): the game's curves every --hours round the clock, each value scaled by its ratio to --hour's,
    so --hour's keyframe is the calibrated environment above.
    - the sun: the trajectory's light (the moon's by night: the game swaps them in its light direction), its colour's
      hue; strength r_sceneSun x the colour's brightest channel over --hour's (moonlight ~0.26 of noon's sun)
    - ambient: x m_globalLight.envProbeAmbientScaleShadow over --hour's (the probes' light in shade: 0.12 at
      midnight, 3.5 at 14:00)
    - the sky: its colours at the hour x the sky's scale with m_sky.globalSkyBrightness at the hour
    - the haze: its colour and density at the hour; the lights: r_sceneLightScale (0.03) x m_colorGroups.lightsDefault
      over --hour's
    - lamps: the city lamps (maps/<map>.xlt) lit while the drawn sun stands under --lamp-elevation, over --lamp-fade"""
    def sun_at(h):
        c = curve_at('m_globalLight.sunColor', (255, 255, 255, 1), h)
        return c, max(max(c[:3]), 0.0)
    def scalar(name, default, h):
        return curve_at(name, (0, 0, 0, default), h)[3]
    _c, sun0 = sun_at(a.hour)
    amb0 = scalar('m_globalLight.envProbeAmbientScaleShadow', 1.0, a.hour)
    gsb0 = scalar('m_sky.globalSkyBrightness', 7.6, a.hour)
    lights0 = scalar('m_colorGroups.lightsDefault', 1.0, a.hour)
    sunscale0 = curve_at('m_globalLight.sunColor', (255, 255, 255, 1), a.hour)[3]
    # the lamps: where the drawn sun crosses the elevation, evening and morning
    def drawn_elev(h):
        v = [float(x) for x in world.trajectory.sun_direction(h * 3600.0)]
        return math.degrees(math.asin(max(-1.0, min(1.0, v[2] / (math.sqrt(sum(x * x for x in v)) or 1.0)))))
    on = off = None
    steps = 24 * 60
    for i in range(steps):
        h0, h1 = 24.0 * i / steps, 24.0 * (i + 1) / steps
        e0, e1 = drawn_elev(h0), drawn_elev(h1)
        if e0 >= a.lamp_elevation > e1:
            on = h1
        if e0 < a.lamp_elevation <= e1:
            off = h1
    out = ['', '# the day/night cycle (w3env.py --hours %g): r_sceneCycle runs the clock' % a.hours,
           'cycle %.2f' % a.hour]
    if on is not None and off is not None:
        out.append('lamps %.2f %.2f %.2f' % (on, off, a.lamp_fade))
    gsb_b = scalar('m_sky.globalSkyBrightness', 7.6, a.hour)
    sky_b = a.sky * gsb_b / max(gsb0, 1e-4) if sky_given else a.sun * gsb_b / max(sunscale0, 1e-3)
    hor_b = [srgb_to_linear(max(x, 0.0) / 255.0) * sky_b
             for x in curve_at('m_sky.skyColorHorizon', (230, 186, 158, 1), a.hour)[:3]]
    sky_lum0 = 0.2126 * hor_b[0] + 0.7152 * hor_b[1] + 0.0722 * hor_b[2]
    n = max(1, int(round(24.0 / a.hours)))
    for i in range(n):
        h = 24.0 * i / n
        c, s = sun_at(h)
        sc = hue(c) if s > 1e-3 else [1.0, 1.0, 1.0]
        light = [float(x) for x in world.trajectory.light_direction(h * 3600.0)]
        yaw = math.degrees(math.atan2(light[1], light[0])) % 360.0
        elev = math.degrees(math.asin(max(-1.0, min(1.0, light[2] / (math.sqrt(sum(x * x for x in light)) or 1.0)))))
        amb = curve_at('m_globalLight.envProbeBaseLightingAmbient.colorAmbient', (255, 255, 255, 1), h)
        ambient = [x * a.ambient * scalar('m_globalLight.envProbeAmbientScaleShadow', 1.0, h) / max(amb0, 1e-4)
                   for x in hue(amb)]
        gsb = scalar('m_sky.globalSkyBrightness', 7.6, h)
        sky = a.sky * gsb / max(gsb0, 1e-4) if sky_given else a.sun * gsb / max(sunscale0, 1e-3)
        zen = [srgb_to_linear(max(x, 0.0) / 255.0) * sky for x in curve_at('m_sky.skyColor', (108, 158, 230, 1), h)[:3]]
        hor = [srgb_to_linear(max(x, 0.0) / 255.0) * sky
               for x in curve_at('m_sky.skyColorHorizon', (230, 186, 158, 1), h)[:3]]
        ff = [max(0.0, min(255.0, x)) for x in curve_at('m_globalFog.fogColorFront', (180, 185, 190, 1), h)]
        fm = [max(0.0, min(255.0, x)) for x in curve_at('m_globalFog.fogColorMiddle', (180, 185, 190, 1), h)]
        dens = scalar('m_globalFog.fogDensity', 0.0006, h) * a.fog
        ls = 0.03 * scalar('m_colorGroups.lightsDefault', 1.0, h) / max(lights0, 1e-4)
        # the horizon toward a low sun: sunColorHorizon, its share sunInfluence as the drawn sun falls under
        # --side-elevation; the glow round the light by night: moonColorSky x moonColorSkyBrightness x moonInfluence
        # over moonAreaSkySize (the light is the moon's then)
        side_w = scalar('m_sky.sunInfluence', 0.0, h) * max(0.0, min(1.0, (a.side_elevation - drawn_elev(h)) /
                                                                  max(a.side_elevation, 1e-3)))
        side = [srgb_to_linear(max(x, 0.0) / 255.0) * sky
                for x in curve_at('m_sky.sunColorHorizon', (0, 0, 0, 1), h)[:3]]
        moon_k = max(0.0, scalar('m_sky.moonColorSkyBrightness', 0.0, h)) * max(0.0, scalar('m_sky.moonInfluence', 0.0, h))
        moon_k = moon_k if moon_k >= 0.01 else 0.0     # (by day ~0.002: none, so --hour's sky is the calibrated one)
        glow = [srgb_to_linear(max(x, 0.0) / 255.0) * sky * moon_k
                for x in curve_at('m_sky.moonColorSky', (0, 0, 0, 1), h)[:3]]
        glow_pow = scalar('m_sky.moonAreaSkySize', 10.0, h)
        # the clouds' light (the renderer's stand-in, lit by the scene's sun): kept in the sky's proportion, the sky's
        # brightest horizon over the sun's, relative to --hour's (else white clouds on a black night sky)
        lum = lambda c: 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]  # noqa: E731
        sky_lum = max(lum(hor), side_w * lum(side))
        k = (sky_lum / max(sky_lum0, 1e-6)) / max(s / max(sun0, 1e-3), 0.25)   # (0.25: the moon's; twilight's ~0)
        # eye adaptation's reach by night (the game's: luminanceLimitMin 0): the window's top raised with the shade's
        # fall from --hour's, up to +1.5 EV
        ev_up = 1.0 + min(1.5, max(0.0, math.log2(max(amb0, 1e-4) / max(scalar('m_globalLight.envProbeAmbientScaleShadow',
                                                                                1.0, h), 1e-4)) / 3.0))
        cloud_light = 1.5 * max(0.05, min(1.0, k))
        out += ['hour %.2f' % h,
                'sun %.3f %.3f %.3f 100 %.2f %.2f' % (sc[0], sc[1], sc[2], yaw, elev),
                'r_sceneSun %.4f' % (a.sun * s / max(sun0, 1e-3)),
                'r_sceneAmbient %.5f %.5f %.5f' % tuple(ambient),
                'r_sceneSkyZenith %.5f %.5f %.5f' % tuple(zen),
                'r_sceneSkyHorizon %.5f %.5f %.5f' % tuple(hor),
                'r_sceneFog %d %d %d' % tuple(round((f + m) / 2) for f, m in zip(ff[:3], fm[:3])),
                'r_sceneFogDensity %.6f' % dens,
                'r_sceneLightScale %.5f' % ls,
                'r_sceneCloudLight %.4f' % cloud_light,
                'r_sceneExposureWindow -1.2 %.3f' % ev_up,
                'r_sceneSkySunSide %.5f %.5f %.5f %.4f' % (side[0], side[1], side[2], side_w),
                'r_sceneSkyGlow %.6f %.6f %.6f %.2f' % (glow[0], glow[1], glow[2], glow_pow)]
    return out


if __name__ == '__main__':
    main()
