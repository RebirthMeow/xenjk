"""Python twin of codemp/game/bg_bike.c (v2: CP2077-style handling data; v3:
two sprung wheels, each on its own ground).

Same equations, same order, float64 throughout. Change both files together;
tape_harness.py --c-exe compares them on the same tapes. The parameters are
the generic bike of BG_BikeDefaultParams, or a handling file read as the game
reads ext_data/vehicles/<name>.bike (default_params(path); tools/bike/ct3x_params.py
writes the CT-3X's from a CP2077 TweakDB dump). v1 (placeholder power model,
lean-driven yaw) is not kept.
"""

import math
from dataclasses import dataclass, field


SUBSTEP_MS = 4
UNITS = 37.5  # units per metre
D2R = math.pi / 180.0
R2D = 180.0 / math.pi
G = 9.81       # m/s^2

EV_STEP = 0x0001
EV_BLOCKED = 0x0002
EV_AIRBORNE = 0x0004
EV_LANDED = 0x0008
EV_BAIL = 0x0010
EV_LOOPOUT = 0x0020
EV_CAPPED = 0x0040
EV_SHIFT = 0x0080
EV_BURNOUT = 0x0100

SURF_ASPHALT, SURF_GROUND, SURF_SAND = 0, 1, 2
SHIFT_MS_MAX = 4000


@dataclass
class Input:
    throttle: float = 0.0
    steer: float = 0.0
    lean: float = 0.0
    handbrake: int = 0


# The generic bike (bg_bike.c BG_BikeDefaultParams, value for value): used without a handling file.
_DEFAULT_GEARS = [(0.0, 3.0, 1000.0, 8000.0, 1.0), (0.0, 12.0, 1000.0, 8500.0, 1.2), (10.0, 18.0, 4000.0, 8500.0, 0.85),
                  (16.0, 24.0, 4500.0, 8500.0, 0.65), (22.0, 30.0, 4500.0, 8500.0, 0.5),
                  (28.0, 36.0, 4500.0, 8500.0, 0.4), (34.0, 40.0, 4500.0, 9000.0, 0.3)]
_DEFAULT_CURVES = {
    'torque': [(0.0, 0.8), (0.5, 0.95), (0.75, 1.0), (1.0, 0.85)],
    'speedToTilt': [(-1.0, 0.2), (0.0, 0.0), (0.25, 0.6), (0.5, 0.85), (1.0, 1.0)],
    'inputToTilt': [(0.0, 0.0), (1.0, 1.0)],
    'speedToTiltSpeed': [(0.0, 1.0), (0.25, 0.95), (1.0, 0.8)],
    'latTraction': [(0.0, 0.0), (0.6, 0.45), (2.0, 0.7), (6.0, 1.0), (10.0, 1.0)],
    'latTractionRear': [(0.0, 0.7), (1.0, 0.9), (3.0, 1.0), (10.0, 1.05)],
}
_PAIRS = ('suspTravel', 'suspOmega', 'dampBump', 'dampBumpLow', 'dampRebound', 'dampReboundLow', 'dampLowRate',
          'dampHighRate')
_INTS = ('maxCommandMs', 'wheelieAssist', 'airPitchStabilize')
_SURFACE_NAMES = ('asphalt', 'ground', 'sand')
MAX_GEARS, MAX_CURVE = 10, 16

# the handling file default_params() reads when given none (tape_harness.py --bike)
BIKE_FILE = None


def generic_params():
    """Mirror of BG_BikeDefaultParams: the generic bike, CP2077-style units."""
    p = dict(
        maxCommandMs=250,
        gravity=G, stepHeight=18.0, originHeight=24.0, originAhead=0.725, frameClearance=0.10, wheelClimb=48.0,
        mass=450.0, inertiaPitch=220.0, wheelbase=1.45, comHeight=0.55, comLongBack=0.65, comLongFwd=0.80,
        comDamp=0.25, airResistance=0.6, forwardWeightTransfer=1.0, lowVelStopDecel=1.0, brakingFriction=1.2,
        handbrakeTorque=3000.0, frameRate=60.0,
        maxWheelTurn=45.0, wheelTurnAdd=120.0, wheelTurnSub=120.0, turnBaseSpeed=8.0, turnMaxSpeed=30.0,
        turnMaxSpeedMul=0.25, maxYawRate=2.0, perfectSteering=1.0, maxTilt=40.0, tiltSpeed=100.0,
        tiltReturnSpeed=100.0, tiltKp=2.0, tiltKi=0.1,
        suspTravel=[0.20, 0.25], suspOmega=[14.0, 13.0], dampBump=[2500.0, 2500.0], dampBumpLow=[1800.0, 1800.0],
        dampRebound=[2200.0, 2200.0], dampReboundLow=[1600.0, 1600.0], dampLowRate=[0.15, 0.15],
        dampHighRate=[0.35, 0.35], bumpStop=10.0, bumpTravel=0.10,
        tireRadiusF=0.30, tireRadiusR=0.32, tireFriction=1.0, latMulF=0.9, latMulR=0.9, longMulF=0.9, longMulR=0.9,
        rollingCoef=0.005, hbLatR=0.7, hbLongR=0.7, burnGrip=5.0, burnLaunchSpeed=5.0, burnMaxSpeed=20.0,
        launchGripMul=1.5, surfaces=[(1.3, 1.0), (1.1, 0.8), (0.9, 0.7)],
        engineTorque=170.0, resistanceTorque=80.0, wheelsResistanceRatio=12.0, finalGearScale=1.0, minRPM=1000.0,
        maxRPM=9000.0, gearChangeTime=0.2, gearChangeCooldown=0.3, gears=list(_DEFAULT_GEARS),
        reverseThreshold=0.5, curves={k: list(c) for k, c in _DEFAULT_CURVES.items()},
        wheelieLean=4.0, wheelieAssist=1, assistMargin=12.0, assistKp=40.0, assistKd=8.0, pitchDamping=1.5,
        maxPitch=70.0, landBailPitch=45.0,
        airPitchMaxVel=6.0, airPitchInputDamp=0.25, airPitchStabilize=1, airPitchZeroAngle=10.0,
        airStabilizeRate=1.0, airYawMaxVel=6.0, airYawInputDamp=0.1,
    )
    p['speedNorm'] = p['gears'][-1][1]
    return p


def parse_bike(text, p):
    """Mirror of BG_BikeParseParams: a handling file (docs/bike-handling.md) over p. Returns the lines not understood."""
    bad, gears, speed_norm_set = [], None, False
    for line in text.splitlines():
        tok = line.split('//', 1)[0].split()
        if not tok:
            continue
        key, first = tok[0], 2 if tok[0].lower() in ('surface', 'curve') else 1
        try:
            v = [float(x) for x in tok[first:]]
        except ValueError:
            bad.append(line)
            continue
        k = key.lower()
        ok = True
        if len(tok) < 2:
            ok = False
        elif k == 'gear':
            if gears is None:
                gears = []
            ok = len(v) == 5 and len(gears) < MAX_GEARS
            if ok:
                gears.append(tuple(v))
        elif k == 'surface':
            name = tok[1].lower()
            ok = len(v) == 2 and name in _SURFACE_NAMES
            if ok:
                p['surfaces'] = list(p['surfaces'])
                p['surfaces'][_SURFACE_NAMES.index(name)] = tuple(v)
        elif k == 'curve':
            names = {n.lower(): n for n in p['curves']}
            ok = len(v) >= 2 and len(v) % 2 == 0 and len(v) // 2 <= MAX_CURVE and tok[1].lower() in names
            if ok:
                p['curves'] = dict(p['curves'])
                p['curves'][names[tok[1].lower()]] = [(v[i], v[i + 1]) for i in range(0, len(v), 2)]
        else:
            field = next((f for f in p if f.lower() == k and f not in ('gears', 'surfaces', 'curves')), None)
            if field is None:
                ok = False
            elif field in _PAIRS:
                ok = len(v) == 2
                if ok:
                    p[field] = list(v)
            else:
                ok = len(v) == 1
                if ok:
                    p[field] = int(v[0]) if field in _INTS else v[0]
                    speed_norm_set |= field == 'speedNorm'
        if not ok:
            bad.append(line)
    if gears is not None:
        if len(gears) < 2:
            bad.append('(fewer than two gears)')
            gears = list(_DEFAULT_GEARS)
        p['gears'] = gears
        if not speed_norm_set:
            p['speedNorm'] = gears[-1][1]
    return bad


def default_params(path=None):
    """The bike's parameters: the handling file at path (or BIKE_FILE) over the generic bike, as the game reads
    ext_data/vehicles/<name>.bike; the generic bike alone without one."""
    p = generic_params()
    path = path or BIKE_FILE
    if path:
        with open(path, encoding='ascii') as f:
            bad = parse_bike(f.read(), p)
        if bad:
            raise ValueError('%s: %d line(s) not understood, first %r' % (path, len(bad), bad[0]))
    return p


@dataclass
class State:
    origin: list = field(default_factory=lambda: [0.0, 0.0, 0.0])
    velocity: list = field(default_factory=lambda: [0.0, 0.0, 0.0])
    yaw: float = 0.0
    yawRate: float = 0.0       # deg/s, + = right
    lean: float = 0.0
    leanInt: float = 0.0       # tilt PID integral, deg s
    pitch: float = 0.0
    pitchRate: float = 0.0
    steer: float = 0.0
    speed: float = 0.0
    comLong: float = 0.0       # m, COM ahead of the rear axle (damped rider lean)
    gear: int = 1              # 0 reverse, 1..7
    rpm: float = 0.0           # derived from gear and speed each step (not state)
    shiftMs: int = SHIFT_MS_MAX  # ms since the last gear change started (capped)
    burnout: int = 0
    burnLaunch: int = 0        # launching off a burnout (its grip bonus applies)
    onGround: int = 1
    commandTime: int = 0
    events: int = 0
    droppedMs: int = 0
    # the last substep's wheels [front, rear] (outputs, not state)
    contact: list = field(default_factory=lambda: [1, 1])
    load: list = field(default_factory=lambda: [0.0, 0.0])
    comp: list = field(default_factory=lambda: [0.0, 0.0])


class World:
    """ground_height(x, y) -> z or None. translate: optional override. surface(x, y) -> SURF_*, optional.
    wheel_ground(wheel, top, depth) -> (z, normal), None, or 'solid' (top is inside something), optional:
    ground_height, its slope by central differences."""

    def __init__(self, ground_height, translate=None, surface=None, wheel_ground=None):
        self.ground_height = ground_height
        self.translate = translate
        self.surface = surface
        self.wheel_ground = wheel_ground


def _clamp(v, lo, hi):
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


def _approach(cur, target, step):
    if cur < target:
        cur += step
        return target if cur > target else cur
    cur -= step
    return target if cur < target else cur


def _curve(c, x):
    """Piecewise linear through the keys, clamped at both ends."""
    if x <= c[0][0]:
        return c[0][1]
    n = len(c)
    for i in range(1, n):
        if x <= c[i][0]:
            x0, y0 = c[i - 1]
            x1, y1 = c[i]
            if x1 <= x0:
                return y1
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return c[n - 1][1]


def _sign(v):
    return 1.0 if v > 0 else (-1.0 if v < 0 else 0.0)


def init_state(world, x, y, yaw, command_time, p=None):
    p = p or default_params()
    z = world.ground_height(x, y)
    s = State()
    s.origin = [x, y, (z if z is not None else 0.0) + p['originHeight']]
    s.yaw = yaw
    s.commandTime = command_time
    s.comLong = 0.5 * (p['comLongBack'] + p['comLongFwd'])
    s.rpm = p['minRPM']
    return s


def turn_mul(p, v):
    """CP2077 turnUpdate: full lock up to the base speed, down to turnMaxSpeedMul at the max speed (m/s)."""
    if v <= p['turnBaseSpeed']:
        return 1.0
    if v >= p['turnMaxSpeed']:
        return p['turnMaxSpeedMul']
    f = (v - p['turnBaseSpeed']) / (p['turnMaxSpeed'] - p['turnBaseSpeed'])
    return 1.0 + (p['turnMaxSpeedMul'] - 1.0) * f


def land(s, p):
    """A wheel touches down after flight; the springs take it from there."""
    if abs(s.pitch) > p['landBailPitch']:
        s.events |= EV_BAIL
    s.events |= EV_LANDED


def heightfield_translate(world, s, p, dt):
    """The body moves by its velocity; a rise above stepHeight under the next rear contact is a wall."""
    nx = s.origin[0] + s.velocity[0] * dt
    ny = s.origin[1] + s.velocity[1] * dt
    gz = world.ground_height(nx, ny)
    if gz is not None and gz > s.origin[2] - p['originHeight'] + p['stepHeight']:
        s.velocity[0] = s.velocity[1] = 0.0
        s.events |= EV_BLOCKED
        nx, ny = s.origin[0], s.origin[1]
    s.origin[0] = nx
    s.origin[1] = ny
    s.origin[2] += s.velocity[2] * dt


HF_STEP = 1.0   # u, the heightfield slope's central differences
HF_EDGE = 0.7   # steeper than walkable is an edge


def _along(bx, bz, c, sn):
    return bx * c - bz * sn


def _up(bx, bz, c, sn):
    return bx * sn + bz * c


def wheels(s, p, world):
    """Mirror of BikeWheels: each wheel's ground, compression (m) and spring load (N)."""
    th = s.pitch * D2R
    c, sn = math.cos(th), math.sin(th)
    yaw_r = s.yaw * D2R
    cy, sy = math.cos(yaw_r), math.sin(yaw_r)
    qz = s.origin[2] - p['originHeight']
    om = s.pitchRate * D2R
    xm = 0.5 * (p['comLongBack'] + p['comLongFwd'])
    out = []
    for i in range(2):
        bx = p['wheelbase'] * UNITS if i == 0 else 0.0
        r = (p['tireRadiusF'] if i == 0 else p['tireRadiusR']) * UNITS
        along = _along(bx, r, c, sn)
        bottom = qz + _up(bx, r, c, sn) - r
        sag = p['gravity'] / (p['suspOmega'][i] * p['suspOmega'][i])
        travel = p['suspTravel'][i]
        share = xm / p['wheelbase'] if i == 0 else 1.0 - xm / p['wheelbase']
        k = p['mass'] * share * p['suspOmega'][i] * p['suspOmega'][i]
        ahead = along - p['originAhead'] * UNITS
        top = (s.origin[0] + cy * ahead, s.origin[1] + sy * ahead, bottom + (travel - sag) * UNITS + p['stepHeight'])
        depth = travel * UNITS + p['stepHeight']
        n = [0.0, 0.0, 1.0]
        gz = 0.0
        if world.wheel_ground:
            res = world.wheel_ground(i, top, depth)
            if res == 'solid':
                res = world.wheel_ground(i, (top[0], top[1], top[2] + p['wheelClimb']), p['wheelClimb'])
                if res == 'solid':
                    res = None
            hit = res is not None
            if hit:
                gz, n = res[0], list(res[1])
        else:
            z = world.ground_height(top[0], top[1])
            hit = z is not None
            if hit:
                gz = z
                if gz > top[2] + p['wheelClimb']:
                    hit = False   # a wall
                elif gz < top[2] - depth:
                    hit = False
            if hit:
                zx0 = world.ground_height(top[0] - HF_STEP, top[1])
                zx1 = world.ground_height(top[0] + HF_STEP, top[1])
                zy0 = world.ground_height(top[0], top[1] - HF_STEP)
                zy1 = world.ground_height(top[0], top[1] + HF_STEP)
                if None not in (zx0, zx1, zy0, zy1):
                    n0 = -(zx1 - zx0) / (2.0 * HF_STEP)
                    n1 = -(zy1 - zy0) / (2.0 * HF_STEP)
                    ln = math.sqrt(n0 * n0 + n1 * n1 + 1.0)
                    n = [n0 / ln, n1 / ln, 1.0 / ln]
                    if n[2] < HF_EDGE:   # a step's edge between two flat faces: the wheel rides a face
                        n = [0.0, 0.0, 1.0]
        comp = sag + (gz - bottom) / UNITS if hit else 0.0
        w = dict(contact=0, comp=0.0, load=0.0, gz=gz, normal=n)
        if not hit or comp <= 0.0 or n[2] <= 0.0:
            out.append(w)
            continue
        v_axle = s.velocity[2] + om * along
        v_ground = -(n[0] * s.velocity[0] + n[1] * s.velocity[1]) / n[2]
        rate = (v_ground - v_axle) / UNITS
        a = abs(rate)
        lo = p['dampBumpLow'][i] if rate >= 0.0 else p['dampReboundLow'][i]
        hi = p['dampBump'][i] if rate >= 0.0 else p['dampRebound'][i]
        if a <= p['dampLowRate'][i]:
            d = lo
        elif a >= p['dampHighRate'][i]:
            d = hi
        else:
            d = lo + (hi - lo) * (a - p['dampLowRate'][i]) / (p['dampHighRate'][i] - p['dampLowRate'][i])
        a = comp if comp < travel + p['bumpTravel'] else travel + p['bumpTravel']
        f = k * a + d * rate
        if a > travel:
            f += p['bumpStop'] * k * (a - travel)
        w.update(contact=1, comp=comp, load=f if f > 0.0 else 0.0)
        out.append(w)
    return out


def body(s, p, w, throttle, lean_in, a_long, v_abs, dt):
    """Mirror of BikeBody: heave and pitch on the springs about the centre of mass."""
    m, inertia = p['mass'], p['inertiaPitch']
    inertia_rear = inertia + m * (s.comLong * s.comLong + p['comHeight'] * p['comHeight'])
    th = s.pitch * D2R
    om = s.pitchRate * D2R
    c, sn = math.cos(th), math.sin(th)
    yaw_r = s.yaw * D2R
    cy, sy = math.cos(yaw_r), math.sin(yaw_r)
    xc, hc = s.comLong * UNITS, p['comHeight'] * UNITS
    qz = s.origin[2] - p['originHeight']
    com_along, com_up = _along(xc, hc, c, sn), _up(xc, hc, c, sn)
    fz = -m * p['gravity']
    tau = 0.0
    slope = 0.0
    gw = w[1] if w[1]['contact'] else w[0]
    for i in range(2):
        if not w[i]['contact']:
            continue
        bx = p['wheelbase'] * UNITS if i == 0 else 0.0
        r = (p['tireRadiusF'] if i == 0 else p['tireRadiusR']) * UNITS
        fz += w[i]['load']
        tau += w[i]['load'] * (_along(bx, r, c, sn) - com_along) / UNITS
    if s.onGround:
        n = gw['normal']
        slope = math.atan2(-(n[0] * cy + n[1] * sy), n[2])
        tau += m * a_long * (qz + com_up - gw['gz']) / UNITS
        wa = th - slope
        balance = math.atan2(s.comLong, p['comHeight'])
        if w[1]['contact'] and lean_in < 0 and throttle > 0:
            f = 1.0 - wa / balance
            tau += inertia_rear * -lean_in * throttle * p['wheelieLean'] * (f if f > 0.0 else 0.0)
        if w[1]['contact'] and not w[0]['contact']:
            if p['wheelieAssist']:
                start = balance - p['assistMargin'] * D2R
                if wa > start:
                    tau -= inertia_rear * (p['assistKp'] * (wa - start) + p['assistKd'] * om)
            tau -= inertia_rear * p['pitchDamping'] * om
        alpha = tau / inertia
    else:
        if lean_in != 0:
            target = -lean_in * p['airPitchMaxVel']
        elif p['airPitchStabilize'] and abs(s.pitch) > p['airPitchZeroAngle']:
            target = -th * p['airStabilizeRate']
        else:
            target = om
        k = 1.0 - math.pow(1.0 - p['airPitchInputDamp'], dt * p['frameRate'])
        alpha = (target - om) * k / dt
    a_q = fz / m * UNITS - (com_along * alpha - com_up * om * om)
    vz = s.velocity[2] + a_q * dt
    om += alpha * dt
    if w[0]['contact'] and w[1]['contact'] and v_abs < 0.05 and abs(vz) < 0.15 and abs(om) < 0.0025:
        vz = 0.0
        om = 0.0
    th += om * dt
    if s.onGround and th - slope > p['maxPitch'] * D2R:
        th = slope + p['maxPitch'] * D2R
        om = 0.0
        s.events |= EV_LOOPOUT
    s.velocity[2] = vz
    s.pitch = th * R2D
    s.pitchRate = om * R2D


def gear_rpm(p, gear, v):
    g = p['gears'][gear]
    f = _clamp((v - g[0]) / (g[1] - g[0]), 0.0, 1.0) if g[1] > g[0] else 0.0
    return g[2] + (g[3] - g[2]) * f


def substep(s, p, inp, world, dt):
    """One fixed step, SI inside (m, s, kg, N); state positions and velocities in JKA units."""
    throttle = _clamp(inp.throttle, -1.0, 1.0)
    steer_in = _clamp(inp.steer, -1.0, 1.0)
    lean_in = _clamp(inp.lean, -1.0, 1.0)
    m = p['mass']
    L = p['wheelbase']

    # 1. steer slew (wheelTurnMaxAdd/SubPerSecond over maxWheelTurnDeg)
    steer = s.steer
    if abs(steer_in) > abs(steer) and steer_in * steer >= 0:
        rate = p['wheelTurnAdd'] / p['maxWheelTurn']
    else:
        rate = p['wheelTurnSub'] / p['maxWheelTurn']
    steer = _approach(steer, steer_in, rate * dt)
    s.steer = steer

    # 2. planar velocity along the heading, m/s
    yaw_r = s.yaw * D2R
    cy = math.cos(yaw_r)
    sy = math.sin(yaw_r)
    fwd = (s.velocity[0] * cy + s.velocity[1] * sy) / UNITS
    lat = (s.velocity[0] * sy - s.velocity[1] * cy) / UNITS
    v_abs = abs(fwd)

    # 3. rider COM (bikeMax/MinCOMLongOffset, damped by bikeCOMOffsetDampFactor per frame)
    mid = 0.5 * (p['comLongBack'] + p['comLongFwd'])
    if lean_in < 0:
        com_t = mid + (p['comLongBack'] - mid) * (-lean_in)
    else:
        com_t = mid + (p['comLongFwd'] - mid) * lean_in
    k = 1.0 - math.pow(1.0 - p['comDamp'], dt * p['frameRate'])
    s.comLong += (com_t - s.comLong) * k

    mu = p['tireFriction']
    hb = 1 if inp.handbrake else 0

    # 4. the wheels: the ground under each, the suspension, the springs' loads (the tyres' normal loads)
    w = wheels(s, p, world)
    was_on_ground = s.onGround
    s.onGround = 1 if (w[0]['contact'] or w[1]['contact']) else 0
    if s.onGround and not was_on_ground:
        land(s, p)
    elif not s.onGround and was_on_ground:
        s.events |= EV_AIRBORNE
    s.contact = [w[0]['contact'], w[1]['contact']]
    s.load = [w[0]['load'], w[1]['load']]
    s.comp = [w[0]['comp'], w[1]['comp']]
    n_f = w[0]['load']
    n_r = w[1]['load']
    # the surface under the wheels as this substep found them
    surf = world.surface(s.origin[0], s.origin[1]) if world.surface else SURF_ASPHALT
    if surf < 0 or surf >= len(p['surfaces']):
        surf = SURF_ASPHALT
    mu_lat_s, mu_long_s = p['surfaces'][surf]

    # 5. gears, rpm, drive (engine torque x curve x gear multiplier x ratio / tyre radius)
    s.events &= ~EV_BURNOUT
    s.shiftMs = min(SHIFT_MS_MAX, s.shiftMs + int(round(dt * 1000.0)))
    shifting = s.shiftMs < p['gearChangeTime'] * 1000.0
    cooling = s.shiftMs < p['gearChangeCooldown'] * 1000.0
    if throttle < 0 and fwd <= p['reverseThreshold']:
        want = 0
    elif s.gear == 0 and (throttle > 0 or fwd > p['reverseThreshold']):
        want = 1
    else:
        want = s.gear
        if want >= 1 and not cooling:
            if v_abs > p['gears'][want][1] and want < len(p['gears']) - 1:
                want += 1
            elif v_abs < p['gears'][want][0] and want > 1:
                want -= 1
    if want != s.gear:
        if s.gear != 0 and want != 0:
            s.shiftMs = 0
            shifting = True
            s.events |= EV_SHIFT
        s.gear = want
    gr = p['gears'][s.gear]
    s.rpm = gear_rpm(p, s.gear, v_abs)
    rpm_f = _clamp((s.rpm - p['minRPM']) / (p['maxRPM'] - p['minRPM']), 0.0, 1.0)
    ratio = p['wheelsResistanceRatio'] * (p['finalGearScale'] if s.gear == len(p['gears']) - 1 else 1.0)
    wheel_k = gr[4] * ratio / p['tireRadiusR']            # wheel force per engine N m
    # burnout (throttle with the handbrake held, slow): the rear spins in place; releasing launches with grip
    if hb and throttle > 0 and v_abs < p['burnMaxSpeed'] and w[1]['contact']:
        s.burnout = 1
        s.events |= EV_BURNOUT
    elif s.burnout:
        s.burnout = 0
        if throttle > 0:
            s.burnLaunch = 1
    # launch grip: x launchGripMul fading out by burnLaunchSpeed, or a burnout's bonus fading the same way
    fade = max(0.0, 1.0 - v_abs / p['burnLaunchSpeed'])
    if s.burnLaunch and (throttle <= 0 or fade <= 0.0):
        s.burnLaunch = 0
    launch_grip = 1.0 + ((p['burnGrip'] if s.burnLaunch else p['launchGripMul']) - 1.0) * fade
    f_eng = 0.0
    if w[1]['contact'] and not shifting and not s.burnout:
        limiter = v_abs >= gr[1] and (s.gear == 0 or s.gear == len(p['gears']) - 1)
        if s.gear == 0 and throttle < 0 and not limiter:
            f_eng = throttle * p['engineTorque'] * _curve(p['curves']['torque'], rpm_f) * wheel_k
        elif s.gear >= 1 and throttle > 0 and fwd >= 0 and not limiter:
            f_eng = throttle * p['engineTorque'] * _curve(p['curves']['torque'], rpm_f) * wheel_k
    brake = 0.0
    if (throttle < 0 and fwd > p['reverseThreshold']) or (throttle > 0 and fwd < 0):
        brake = abs(throttle)
    f_air = p['airResistance'] * fwd * abs(fwd)

    grip = mu * p['longMulR'] * mu_long_s * n_r * launch_grip
    f_drive = _clamp(f_eng, -grip, grip)
    # the slope: the horizontal part of each tyre's normal push (its vertical part is the spring's load)
    f_grade = 0.0
    for i in range(2):
        if w[i]['contact']:
            n = w[i]['normal']
            f_grade += w[i]['load'] * (n[0] * cy + n[1] * sy) / n[2]

    # 6. resistances and brakes (all oppose motion)
    f_res = 0.0
    if s.onGround:
        f_res += p['rollingCoef'] * (n_f + n_r)
        if throttle == 0 and s.gear >= 1 and w[1]['contact']:   # engine braking ([interpreted] scaled by rpm)
            f_res += p['resistanceTorque'] * rpm_f * wheel_k
        if brake > 0:   # [interpreted] friction-limited, scaled by brakingFrictionFactor
            f_res += brake * p['brakingFriction'] * mu * mu_long_s * (p['longMulF'] * n_f + p['longMulR'] * n_r)
        if hb and not s.burnout:
            f_res += min(p['handbrakeTorque'] / p['tireRadiusR'], mu * mu_long_s * p['hbLongR'] * p['longMulR'] * n_r)
        if v_abs < 1.0 and throttle == 0:   # [xenjk] let go on a hill, it holds on its brakes
            f_res += p['lowVelStopDecel'] * m
            f_res += p['brakingFriction'] * mu * mu_long_s * (p['longMulF'] * n_f + p['longMulR'] * n_r)
    new_fwd = fwd + (f_drive - f_air + f_grade) / m * dt
    dv = f_res / m * dt
    if abs(new_fwd) <= dv:
        new_fwd = 0.0
    elif new_fwd > 0:
        new_fwd -= dv
    else:
        new_fwd += dv
    a_long = (new_fwd - fwd) / dt

    # 7. lateral: slip decays by the surface's lateral traction curves, front and rear by load
    if s.onGround:
        lat_abs = abs(lat)
        trac_f = _curve(p['curves']['latTraction'], lat_abs) * p['latMulF'] * n_f
        trac_r = _curve(p['curves']['latTractionRear'], lat_abs) * p['latMulR'] * n_r * (p['hbLatR'] if hb else 1.0)
        a_lat = mu * mu_lat_s * (trac_f + trac_r) / m
        dl = a_lat * dt
        lat = 0.0 if lat_abs <= dl else lat - _sign(lat) * dl
    new_lat = lat

    # 8. yaw: perfectSteeringFactor 1, the bike follows its front wheel; capped by lateral grip and the
    #    RotationLimiter (maxAngularSpeedRad; the handbrake lets it rotate past grip: a power slide)
    if s.onGround:
        delta = steer * p['maxWheelTurn'] * turn_mul(p, v_abs) * D2R
        r = new_fwd * math.tan(delta) / L
        if not hb and v_abs > 0.1:
            cap = mu * mu_lat_s * (p['latMulF'] * n_f + p['latMulR'] * n_r) / (m * v_abs)
            r = _clamp(r, -cap, cap)
        r = _clamp(r, -p['maxYawRate'], p['maxYawRate'])
        s.yawRate = r * R2D
    else:
        target = steer * p['airYawMaxVel'] * R2D
        k = 1.0 - math.pow(1.0 - p['airYawInputDamp'], dt * p['frameRate'])
        s.yawRate += (target - s.yawRate) * k
    yaw = s.yaw - s.yawRate * dt
    if yaw > 180.0:
        yaw -= 360.0
    elif yaw <= -180.0:
        yaw += 360.0
    s.yaw = yaw
    # perfectSteeringFactor: the velocity turns with the heading by this share; the handbrake lets the heading
    # turn under it (a slide), which the lateral traction then pulls back
    if s.onGround and not hb:
        vyaw = yaw_r - s.yawRate * dt * p['perfectSteering'] * D2R
        cy = math.cos(vyaw)
        sy = math.sin(vyaw)
    vx = (new_fwd * cy + new_lat * sy) * UNITS
    vy = (new_fwd * sy - new_lat * cy) * UNITS
    s.velocity[0] = vx
    s.velocity[1] = vy

    # 9. tilt: target from speed_to_tilt x input_to_tilt, rate limited by speed_to_tilt_speed, bikeTiltPID
    vn = new_fwd / p['speedNorm']
    lean = s.lean
    if s.onGround:
        target = _sign(steer) * p['maxTilt'] * _curve(p['curves']['speedToTilt'], _clamp(vn, -1.0, 1.0)) \
            * _curve(p['curves']['inputToTilt'], abs(steer))
        err = target - lean
        s.leanInt = _clamp(s.leanInt + err * dt, -p['maxTilt'], p['maxTilt'])
        cmd = p['tiltKp'] * err + p['tiltKi'] * s.leanInt
        lim = (p['tiltSpeed'] if abs(target) > abs(lean) else p['tiltReturnSpeed']) \
            * _curve(p['curves']['speedToTiltSpeed'], _clamp(abs(vn), 0.0, 1.0))
        cmd = _clamp(cmd, -lim, lim)
        lean = _clamp(lean + cmd * dt, -p['maxTilt'], p['maxTilt'])
    s.lean = lean

    # 10. heave and pitch on the springs
    body(s, p, w, throttle, lean_in, a_long, v_abs, dt)

    # 11. translate inside the substep (the wheels find the ground at the start of the next)
    vb = (s.velocity[0], s.velocity[1])
    if world.translate:
        world.translate(world, s, p, dt)
    else:
        heightfield_translate(world, s, p, dt)

    # 12. a wall stopped what this step's drive added: its push acts at the front axle's height, not the ground
    vb_len = math.sqrt(vb[0] * vb[0] + vb[1] * vb[1])
    if s.onGround and vb_len > 0.0:
        lost = (vb_len - (vb[0] * s.velocity[0] + vb[1] * s.velocity[1]) / vb_len) / UNITS
        added = abs(new_fwd) - abs(fwd)
        if lost > added:
            lost = added
        if lost > 0.0:
            s.pitchRate -= _sign(new_fwd) * m * lost * (p['comHeight'] - p['tireRadiusF']) / p['inertiaPitch'] * R2D

    s.speed = new_fwd * UNITS


def run_command(s, p, inp, world, server_time, on_substep=None):
    """Grid mode: mirror of BG_BikeRunCommand. on_substep(grid_index, s)."""
    s.events = 0
    if server_time <= s.commandTime:
        return 0
    g0 = s.commandTime // SUBSTEP_MS
    n = server_time // SUBSTEP_MS - g0
    cap = p['maxCommandMs'] // SUBSTEP_MS
    first = g0 + 1
    if n > cap:
        s.droppedMs += (n - cap) * SUBSTEP_MS
        s.events |= EV_CAPPED
        first = g0 + 1 + (n - cap)  # dropped time comes first; trace keys stay on the grid
        n = cap
    for i in range(n):
        substep(s, p, inp, world, SUBSTEP_MS / 1000.0)
        if on_substep:
            on_substep(first + i, s)
    s.commandTime = server_time
    return n


def run_command_naive(s, p, inp, world, server_time):
    """Control: one variable step per command, as stock vehicle code does."""
    s.events = 0
    msec = server_time - s.commandTime
    if msec <= 0:
        return 0
    if msec > p['maxCommandMs']:
        msec = p['maxCommandMs']
    substep(s, p, inp, world, msec / 1000.0)
    s.commandTime = server_time
    return 1


def snapshot(s):
    return (s.origin[0], s.origin[1], s.origin[2], s.yaw, s.lean, s.pitch, s.speed)
