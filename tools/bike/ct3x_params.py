"""CT-3X handling data (ct3x_handling.json, from tools/bike/tweakdb.py) -> the bike's handling file.

Writes ct3x.bike, which goes in the bike's pack as ext_data/vehicles/ct3x.bike (the name is the .veh vehicle's
name). Game and cgame read it when the vehicle loads (codemp/game/bg_bikemove.c BG_BikeLoadParams, parsed by
bg_bike.c BG_BikeParseParams) and tools/bike/bike_twin.py reads the same file (tape_harness.py --bike), so the two
integrators cannot drift. Format: docs/bike-handling.md. Without the file a bike drives on bg_bike.c's generic
handling, so nothing in the repository carries CP2077's numbers: generate the file from your own copy of the game.

Every value keeps CP2077's units (metres, kg, N m, seconds, degrees); bg_bike converts to JKA units (37.5 u/m) at
init. Curves keep their key points; the module interpolates linearly between them (CP2077 draws some as Bezier or
Hermite through the same keys: close at the keys, approximate between).

  python -I ct3x_params.py [HANDLING_JSON] [-o OUT.bike]
  python -I ct3x_params.py --from-header bg_bike_ct3x.h [-o OUT.bike]

HANDLING_JSON defaults to the drafts folder under XENJK_MAIN (the checkout holding .codex-research; default this
repository). --from-header reads the values from a bg_bike_ct3x.h an earlier version of this script wrote (the header
the game was once built with) instead of the TweakDB dump. OUT defaults to ct3x.bike in the current folder.
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
UNITS = 37.5    # JKA units per metre

# the converted model's rig (tools/bike/ct3x_model.py) and xenjk's tuning for this bike: not in the TweakDB data
MODEL = {
    'wheelbase': 1.82,                  # m, wheel_front to wheel_back in the rig's reference pose
    'originAhead': 26.89 / UNITS,       # m: the model's wheel_back bolt is 26.89 u behind its origin
    'frameClearance': 0.10,             # m: the CT-3X's belly, about 10 cm up at rest
    'riderCom': 0.08,                   # m: a JKA rider (75 kg, torso about 1.1 m up) raises CP2077's riderless COM
}
TUNING = {  # xenjk: the rider's pull on the bars; CP2077 wheelies are not in its TweakDB data
    'wheelieLean': 4.0, 'wheelieAssist': 1, 'assistMargin': 12.0, 'assistKp': 40.0, 'assistKd': 8.0,
    'pitchDamping': 1.5, 'maxPitch': 70.0, 'landBailPitch': 45.0,
}
CURVE_NAMES = ('torque', 'speedToTilt', 'inputToTilt', 'speedToTiltSpeed', 'latTraction', 'latTractionRear')


def from_json(src):
    """(V, GEARS, SURF, CURVES) from the TweakDB dump."""
    d = json.load(open(src))
    R = d['records']
    dm = R['Vehicle.VehicleDriveModelDataDefaultYaibaKusanagi']
    eng = R['Vehicle.v_sportbike1_yaiba_kusanagi_inline5']
    gears = [R[g] for g in eng['gears']]          # [0] reverse, then 1..7
    wf = R['Vehicle.VehicleDriveModelDataDefaultYaibaKusanagi_inline1']   # front wheel preset
    wr = R['Vehicle.VehicleDriveModelDataDefaultYaibaKusanagi_inline2']   # back
    dims = R['Vehicle.VehicleWheelDimensionsDefaultYaibaKusanagi']
    dimf, dimr = R[dims['frontPreset']], R[dims['backPreset']]
    hb = R['Vehicle.VehicleDriveModelDataDefaultYaibaKusanagi_inline3']    # HandbrakeFrictionModifier
    rot = R['Vehicle.VehicleDriveModelDataDefaultYaibaKusanagi_inline4']   # RotationLimiter
    burn = R[dm['burnOut']]
    air = R['Vehicle.v_sportbike1_yaiba_kusanagi_player']['vehAirControl']
    air = R[air]
    air_pitch, air_yaw = R[air['pitch']], R[air['yaw']]
    fm = d['friction_map']
    C = d['curves']

    def surface(name):
        p = fm[f'BikeDrivingFrictionPreset.{name}_Default']
        return p['frictionLatMultiplier'], p['frictionLongMultiplier']

    def pts(curve):
        return [tuple(p) for p in curve['points']]

    V = {
        # chassis
        'mass': dm['total_mass'], 'inertiaPitch': dm['momentOfInertia'][0], 'inertiaYaw': dm['momentOfInertia'][2],
        'comOffsetFwd': dm['center_of_mass_offset'][1], 'comOffsetUp': dm['center_of_mass_offset'][2],
        'comLongBack': dm['bikeMaxCOMLongOffset'], 'comLongFwd': dm['bikeMinCOMLongOffset'],
        'comDamp': dm['bikeCOMOffsetDampFactor'],
        'airResistance': dm['airResistanceFactor'], 'forwardWeightTransfer': dm['forwardWeightTransferFactor'],
        'lowVelStopDecel': dm['lowVelStoppingDeceleration'], 'brakingFriction': dm['brakingFrictionFactor'],
        'handbrakeTorque': dm['handbrakeBrakingTorque'],
        # steering and tilt
        'maxWheelTurn': dm['maxWheelTurnDeg'], 'wheelTurnAdd': dm['wheelTurnMaxAddPerSecond'],
        'wheelTurnSub': dm['wheelTurnMaxSubPerSecond'], 'turnBaseSpeed': dm['turnUpdateBaseSpeedThreshold'],
        'turnMaxSpeed': dm['turnUpdateMaxSpeedThreshold'], 'turnMaxSpeedMul': dm['turnUpdateMaxSpeedTurnMul'],
        'maxTilt': dm['bikeMaxTilt'], 'tiltSpeed': dm['bikeTiltSpeed'], 'tiltReturnSpeed': dm['bikeTiltReturnSpeed'],
        'tiltKp': dm['bikeTiltPID'][0], 'tiltKi': dm['bikeTiltPID'][1], 'tiltKd': dm['bikeTiltPID'][2],
        'maxYawRate': rot['maxAngularSpeedRad'], 'perfectSteering': dm['perfectSteeringFactor'],
        # wheels
        'tireRadiusF': dimf['tireRadius'], 'tireRadiusR': dimr['tireRadius'],
        'latMulF': wf['frictionMulLateral'], 'latMulR': wr['frictionMulLateral'],
        'longMulF': wf['frictionMulLongitudinal'], 'longMulR': wr['frictionMulLongitudinal'],
        'tireFriction': wr['tireFrictionCoef'], 'rollingCoef': wr['tireRollingResistanceCoef'],
        'brakeTorqueF': wf['maxBrakingTorque'], 'brakeTorqueR': wr['maxBrakingTorque'],
        'hbLatR': hb['rearWheelsLatFrictionCoef'], 'hbLongR': hb['rearWheelsLongFrictionCoef'],
        'burnGrip': burn['burnOutGripBonus'], 'burnLaunchSpeed': burn['gripBonusMaxLaunchSpeed'],
        'burnMaxSpeed': burn['maxSpeedToInitiateBurnOut'], 'launchGripMul': burn['gripBonusMaxSpeedMultiplier'],
        # suspension, front (F) and back (R) wheel presets
        **{f'{k}{s}': w[v] for s, w in (('F', wf), ('R', wr)) for k, v in (
            ('suspTravel', 'logicalSuspensionCompressionLength'), ('springStiffness', 'springStiffness'),
            ('dampBump', 'springDamping'), ('dampBumpLow', 'springBoundDampingLowRate'),
            ('dampRebound', 'springReboundDamping'), ('dampReboundLow', 'springReboundDampingLowRate'),
            ('dampLowRate', 'springDampingLowRateCompression'), ('dampHighRate', 'springDampingHighRateCompression'))},
        # engine
        'engineTorque': eng['engineMaxTorque'], 'resistanceTorque': eng['resistanceTorque'],
        'wheelsResistanceRatio': eng['wheelsResistanceRatio'], 'minRPM': eng['minRPM'], 'maxRPM': eng['maxRPM'],
        'gearChangeTime': eng['gearChangeTime'], 'gearChangeCooldown': eng['gearChangeCooldown'],
        'finalGearScale': eng['finalGearTorqueDecimationScalor'],
        # air control (pitch FB, yaw LR)
        'airPitchMaxVel': abs(air_pitch['maxVelocity']), 'airPitchInputDamp': air_pitch['inputDampFactor'],
        'airPitchAngleDamp': air_pitch['angleDampFactor'], 'airPitchStabilize': 1 if air_pitch['stabilizeAxis'] else 0,
        'airPitchZeroAngle': air_pitch['zeroAngleThreshold'],
        'airYawMaxVel': abs(air_yaw['maxVelocity']), 'airYawInputDamp': air_yaw['inputDampFactor'],
    }
    V = {k: float(v) for k, v in V.items()}
    GEARS = [(g['minSpeed'], g['maxSpeed'], g['minEngineRPM'], g['maxEngineRPM'], g['torqueMultiplier']) for g in gears]
    SURF = [surface(n) for n in ('Asphalt', 'Ground', 'Sand')]
    CURVES = {
        'torque': pts(C['large_balanced_na.curveset']['1']),
        'speedToTilt': pts(C['default_bike.bikecurveset']['speed_to_tilt']),
        'inputToTilt': pts(C['default_bike.bikecurveset']['input_to_tilt']),
        'speedToTiltSpeed': pts(C['default_bike.bikecurveset']['speed_to_tilt_speed']),
        'latTraction': pts(C['asphalt_surface.curveset']['lat_speed_diff_to_traction']),
        'latTractionRear': pts(C['asphalt_surface.curveset']['lat_speed_diff_to_traction_rear']),
    }
    return V, GEARS, SURF, CURVES


def _rows(text, name):
    """The { a, b, ... } rows of the C array `name` in an old generated header."""
    m = re.search(r'\b%s\s*\[[^]]*\]\s*\[\d+\]\s*=\s*\{(.*?)\};' % re.escape(name), text, re.S)
    if not m:
        raise SystemExit('header: no array %s' % name)
    return [tuple(float(x) for x in r.split(',')) for r in re.findall(r'\{([^{}]*)\}', m.group(1))]


def from_header(path):
    """(V, GEARS, SURF, CURVES) from a bg_bike_ct3x.h written by an earlier version of this script."""
    text = Path(path).read_text(encoding='ascii')
    V = {m.group(1): float(m.group(2)) for m in re.finditer(r'(?m)^#define CT3X_(\w+)\s+(\S+)\s*$', text)
         if not m.group(1).startswith('CURVE_') and m.group(1) != 'NUM_GEARS'}
    GEARS = _rows(text, 'ct3xGears')
    SURF = _rows(text, 'ct3xSurfaces')
    CURVES = {k: _rows(text, 'ct3xCurve_' + k) for k in CURVE_NAMES}
    return V, GEARS, SURF, CURVES


def bike_text(V, GEARS, SURF, CURVES, source):
    """The handling file: bikeParams_t field names (codemp/game/bg_bike.h), one per line."""
    r = repr
    L = ['// ct3x.bike -- GENERATED by tools/bike/ct3x_params.py (%s). Format: docs/bike-handling.md.' % source,
         '// CP2077 units: m, kg, N m, s, deg. Goes in the bike pack as ext_data/vehicles/ct3x.bike.', '']

    def put(key, *vals, note=''):
        L.append(' '.join([key] + [r(float(v)) if not isinstance(v, int) else str(v) for v in vals])
                 + ('\t// ' + note if note else ''))

    L.append('// chassis')
    put('mass', V['mass'])
    put('inertiaPitch', V['inertiaPitch'])
    put('wheelbase', MODEL['wheelbase'], note='the converted model\'s rig (tools/bike/ct3x_model.py)')
    put('originAhead', MODEL['originAhead'], note='the model: the wheel_back bolt is 26.89 u behind its origin')
    put('frameClearance', MODEL['frameClearance'], note='xenjk: the belly, about 10 cm up at rest')
    put('comHeight', V['comOffsetUp'] + V['tireRadiusR'] + MODEL['riderCom'],
        note='center_of_mass_offset + tyre radius + 0.08 m for a JKA rider')
    for k in ('comLongBack', 'comLongFwd', 'comDamp', 'airResistance', 'forwardWeightTransfer', 'lowVelStopDecel',
              'brakingFriction', 'handbrakeTorque'):
        put(k, V[k])
    L += ['', '// steering and tilt']
    for k in ('maxWheelTurn', 'wheelTurnAdd', 'wheelTurnSub', 'turnBaseSpeed', 'turnMaxSpeed', 'turnMaxSpeedMul',
              'maxYawRate', 'perfectSteering', 'maxTilt', 'tiltSpeed', 'tiltReturnSpeed', 'tiltKp', 'tiltKi'):
        put(k, V[k])
    L += ['', '// suspension: front rear ([interpreted] springStiffness as the natural frequency, rad/s)']
    for k, src in (('suspTravel', 'suspTravel'), ('suspOmega', 'springStiffness'), ('dampBump', 'dampBump'),
                   ('dampBumpLow', 'dampBumpLow'), ('dampRebound', 'dampRebound'),
                   ('dampReboundLow', 'dampReboundLow'), ('dampLowRate', 'dampLowRate'),
                   ('dampHighRate', 'dampHighRate')):
        put(k, V[src + 'F'], V[src + 'R'])
    L += ['', '// tyres']
    for k in ('tireRadiusF', 'tireRadiusR', 'tireFriction', 'latMulF', 'latMulR', 'longMulF', 'longMulR',
              'rollingCoef', 'hbLatR', 'hbLongR', 'burnGrip', 'burnLaunchSpeed', 'burnMaxSpeed', 'launchGripMul'):
        put(k, V[k])
    for name, s in zip(('asphalt', 'ground', 'sand'), SURF):
        put('surface ' + name, *s)
    L += ['', '// engine']
    for k in ('engineTorque', 'resistanceTorque', 'wheelsResistanceRatio', 'finalGearScale', 'minRPM', 'maxRPM',
              'gearChangeTime', 'gearChangeCooldown'):
        put(k, V[k])
    L.append('// gears: minSpeed m/s, maxSpeed m/s, minRPM, maxRPM, torqueMultiplier; reverse first (speedNorm '
             'follows the top gear)')
    for g in GEARS:
        put('gear', *g)
    L += ['', '// curves: x y pairs']
    for k in CURVE_NAMES:
        put('curve ' + k, *[c for p in CURVES[k] for c in p])
    L += ['', '// air control']
    for k in ('airPitchMaxVel', 'airPitchInputDamp'):
        put(k, V[k])
    put('airPitchStabilize', int(V['airPitchStabilize']))
    for k in ('airPitchZeroAngle', 'airYawMaxVel', 'airYawInputDamp'):
        put(k, V[k])
    L += ['', '// xenjk tuning for this bike (not in CP2077\'s data)']
    for k, v in TUNING.items():
        put(k, v)
    return '\n'.join(L) + '\n'


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('json', nargs='?', help='ct3x_handling.json (tools/bike/tweakdb.py)')
    ap.add_argument('--from-header', help='an old generated bg_bike_ct3x.h instead of the TweakDB dump')
    ap.add_argument('-o', '--out', default='ct3x.bike')
    a = ap.parse_args(argv)
    if a.from_header:
        data, source = from_header(a.from_header), 'from an old bg_bike_ct3x.h'
    else:
        src = Path(a.json) if a.json else (Path(os.environ.get('XENJK_MAIN', REPO)) / '.codex-research'
                                            / 'wishlist-20261101' / 'drafts' / 'bike' / 'ct3x_handling.json')
        data, source = from_json(src), 'from CP2077 TweakDB, Vehicle.v_sportbike1_yaiba_kusanagi_player'
    Path(a.out).write_text(bike_text(*data, source), encoding='ascii', newline='\n')
    print('wrote', a.out, ':', len(data[1]), 'gears,', len(data[3]), 'curves')


if __name__ == '__main__':
    main(sys.argv[1:])
