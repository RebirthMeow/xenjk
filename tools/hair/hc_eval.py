"""hc_eval.py -- Python access to the engine's hair solver (hc_eval.c, built as a shared library).

    ev = HcEval('hc_eval.dll')
    pos = ev.run(asset_bytes, ref, frame, targets, anchors, rep, init_pos, init_prev, feel=FEEL_GAME, flow=FLOW_GAME)

All arrays are float64 in the solver's reference frame: ref [nS,16] column-major, frame [nS] ints, targets [nS,nP,3],
anchors [nS,nC,3] (only root cones are read), rep [nS,nR,2,3]; returns positions [nS,nP,3] after each step.
FEEL_GAME / FLOW_GAME are the engine's cvar defaults (docs/hair/README.md), so a run sees what the game runs.
"""
import ctypes
import math

import numpy as np

# r_hairFeel 1 with its defaults: lin 1, 1.5 g, tau 0 (unused in carry), euler 1, 9000 deg/s^2, cent 1, 720 deg/s,
# carry lag 300 u/s, angular carry lag 1080 deg/s (engine units: radians)
FEEL_GAME = (1, 1.0, 1.5, 0.0, 1.0, math.radians(9000), 1.0, math.radians(720), 300.0, math.radians(1080))
FEEL_OFF = (0,) * 10
FLOW_GAME = (2.0, 0.5, 3.0, 0.0)   # r_hairFlow 1: springDamp, innerScale, airDrag, coneParent
FLOW_OFF = (0.0, 0.0, 0.0, 0.0)

_D = ctypes.POINTER(ctypes.c_double)


class HcEval:
    def __init__(self, dll):
        self.lib = ctypes.CDLL(dll)
        f = self.lib.hce_run
        f.restype = ctypes.c_int
        f.argtypes = [ctypes.c_char_p, ctypes.c_long, ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
                      ctypes.c_int, ctypes.c_int, _D, ctypes.POINTER(ctypes.c_int), _D, _D, _D, _D, _D, _D,
                      ctypes.c_char_p, ctypes.c_int]

    def run(self, asset, ref, frame, targets, anchors, rep, init_pos, init_prev, feel=FEEL_GAME, flow=FLOW_GAME,
            honour_inverse=False):
        nS, nP = targets.shape[:2]
        c = lambda a: np.ascontiguousarray(a, np.float64)
        ref, targets, anchors, rep, init_pos, init_prev = map(c, (ref, targets, anchors, rep, init_pos, init_prev))
        frame = np.ascontiguousarray(frame, np.int32)
        if rep.size == 0:
            rep = np.zeros(1)
        out = np.zeros((nS, nP, 3))
        fe = (ctypes.c_float * 10)(*feel)
        fl = (ctypes.c_float * 4)(*flow)
        err = ctypes.create_string_buffer(256)
        p = lambda a: a.ctypes.data_as(_D)
        ok = self.lib.hce_run(asset, len(asset), fe, fl, int(honour_inverse), nS, p(ref),
                              frame.ctypes.data_as(ctypes.POINTER(ctypes.c_int)), p(targets), p(anchors), p(rep),
                              p(init_pos), p(init_prev), p(out), err, 256)
        if not ok:
            raise RuntimeError('hce_run: ' + err.value.decode('latin1'))
        return out
