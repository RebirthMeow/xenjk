# Defaults re-measured; base D's walkway falls located (2026-10-05)

Build `gate-build-20261005-a` (HEAD fc061d1e sources): the defaults of 2026-10-05 (`bot_moveSearchAlone` 2048,
walk deferral K = 6 pool 12 with rescue, drop deferral K = 6, climb bound). Retained under
`.codex-research/gate-20261005-a/`, `gate-20261005-b/`, `kejfalls-20261005-a/`, `edgeprobe-20261005-a/`.

## Gates

**Harness mistake, corrected.** `gate-20261005-a` copied `gate-20260926-a`, whose level-1 library
(`slowdrop-chain-20260924-a`) was explored before strafe skill 90 became the default (5fa681f2, 2026-09-26). With it
row 2 failed (median 17,050 ms against 16,500) and the library chose no tape in any world. That library already failed
on the skill-90 build the same day (`gate-20260926-b`); the gates were met only after re-exploring
(`gate-20260926-c`, library `skill90-chain-20260926-a`). Two checks confirmed it was the library, not a regression:

- Deferrals off on today's build (`gate-20261005-a/attrib/`): stand times 14,550 / 17,700 / 17,775 / 18,150 ms, the
  library and control identical, exactly the skill-90 control of `gate-20260926-c`.
- Retained builds from 10-02 to 10-04 (`gate-20261005-a/locate/`, run through `bench_unbound.py` because those
  builds came from uncommitted trees; diagnostic, not source-bound): the same four times on every one. The walk
  deferral default (`walkdefault-build-20261005-a`) moves them to 16,950 / 17,325 / 14,200 / 17,150.

`gate-20261005-b` re-takes the gates with the skill-90 library (same build):

| Row | Result | 2026-09-26 (`gate-20260926-c`) |
| --- | --- | --- |
| L1 safety | floor 36/36, zero deaths, owner `move`/`jumppad`; harness-on parity non-zero, zero mismatches | met |
| L1 speed | library 16,950 / 15,475 / 14,200 / 17,150 ms: median **16,212.5 (bound 16,500), met**; tail 17,150 (18,000) | 14,550 / 12,100 / 13,875 / 12,300, median 13,087.5 |
| L1 mechanism | **not met**: class 125 worst waiting 31.4 against the control's 29.4, class 150 57.7 against 57.3 | met strictly |
| L1 coverage | 36/36 timed; library slower than control on 3 of 12 cases | - |
| ctf4, Dantooine v3 floors | 36/36 each | 36/36 |
| L2 speed | 10,775 / 10,975 / 11,025 / 9,125 ms (bound 13,000), met | 8,150 / 9,100 / 9,875 / 9,575 |
| L2 coverage | 36/36, no deaths (`gate-20261005-a`: tail 14,000) | tail 11,275 |
| Cost, one bot | p99 17.0 / 18.0 ms, 5 and 4 frames over 25 ms per 800, met | 16.5-17.9 |

The library now buys almost nothing (control median 16,337.5): its tapes were explored before the walk deferral
changed the walking decisions, and the 09-26 library gain (17,737.5 -> 13,087.5) is gone. Re-exploring the library
at the current defaults, as on 2026-09-26, is the known remedy; row 3's two misses are within half a point to two
points of the control.

## Kejim 4v4 falls with the deferrals

`kejfalls-20261005-a`: 16 paired seeds, both deferrals off against the default. Both runners' falls +1.48 +- 0.90 a
world, weapon deaths -2.00 +- 1.28 (total runner deaths flat), team captures +0.30 +- 0.40. The earlier pair measured
+1.5 +- 0.9; together about +1.5 +- 0.64, a trade of falls for weapon deaths rather than more deaths.

## Base D's walkway falls

Base D falls 1.9-2.25 times a Kejim 4v4 world, with or without the deferrals. Of 31 classified in
`drop-20261005-b/defer` (`runuphold-20261001-a/falls_mode.py`), most are a `no_path` hold after the bot has left
the graph, at two spots beside its post: the east edge of the red ramp (x 270-285, y -360 to -460; the ramp's cells
end at x 272) and the walkway's inside corner (x 365-400, y -80 to -115).

**Mechanism** (play CSVs, eight falls): base D runs down the ramp or along the walkway at about 410 u/s heading
diagonally toward the edge; the guest momentum brake fires (`walk brake why=floor_missing`) at its stopping distance,
but the corridor's floor samples trace the whole 30-unit hull, so the chord stays floored until the centre is up to
15 u past the edge. The bot stops with its centre over the lip, the hull slides off the ramp's edge (60-200 u/s), the
graph root is lost, and the hold that follows cannot climb back.

**Probe built** (first as `bot_moveGuestEdgeHull H`, default 0, build `edge-build-20261005-a`; now
`bot_moveEdgeHull`, below): a guest's momentum chord is also sampled with a hull H wide
each side, so the brake measures to where the centre leaves the floor. `edgeprobe-20261005-a`, H = 4 against the
default, 16 paired Kejim 4v4 seeds:

| per world | default | H = 4 | paired |
| --- | --- | --- | --- |
| base D falls | 2.25 | 1.56 | -0.69 +- 0.39 |
| chase D falls | 2.38 | 2.50 | +0.12 +- 0.54 |
| camper falls | 5.68 | 4.25 | -1.44 +- 0.84 |
| team captures | 4.07 | 3.82 | -0.25 +- 0.38 |
| pickups | 29.82 | 27.62 | -2.19 +- 0.75 |

**Every bot (owner: "apply it to all"), measured and withdrawn.** `bot_moveEdgeHull` (default 4) on every bot,
48f932da, build `edgeall-build-20261005-b`, gates `gate-20261005-c`: Kejim floor **35/36**, one death -
`blue_out_L1_respawn4` braked at the lip of its own drop every frame (`walk brake why=floor_missing`, creeping
1 u per 50 ms) until it fell; one-bot p99 17.0 / 18.0 -> 25.9 / 23.9 ms (10 and 8 frames over 25 ms); level-1 tail
18,475 (bound 18,000). The runner's routes leave from lips on purpose; a guest's do not. The probe is guests only
(base D, chase D, campers), default 4 (4f3cea52, `edgeguest-build-20261005-a`): floor 36/36, no deaths, and the
pinned level-1 stand worlds identical to `gate-20261005-b` at every class (16,950 / 15,475 / 14,200 / 17,150 ms), so
the gates of `gate-20261005-b` stand for it. The 4v4 pairs begun on the every-bot build were stopped after the first
round (`edgeall-20261005-a`, kept).

Dantooine v3, guests only on against off (`edgeguest-20261005-a`, 16 paired seeds): team captures +0.06 +- 0.34,
pickups -0.89 +- 0.81, base D falls +0.12 +- 0.22 (0.44 a world off: Dantooine has no such ramp), chase D falls
-0.56 +- 0.43, camper falls -0.06 +- 0.54; nothing beyond 2 se. Kejim is the table above (same code, H = 4).

A situation test from 24 recorded ramp moments (`based-ramp-20261005-a`) did not reproduce the fall (one base D fall
in each arm): a fall is rare per ramp run, so only whole worlds measure it.

**Reverted (owner, same day):** "if something breaks offense I think that is bad enough to remove from all" - the gates
cover Kejim offense only, and a probe that stalls a planned lip departure there can do so for any bot on any map. Both
commits reverted (c0d62612, 1bbb23a2); the sources are those of `gate-build-20261005-a` again, so `gate-20261005-b`
is the gate of the current state. The walkway falls stay open: a fix must keep planned departures from lips.

## Edge probe, third form: every bot, routes with no planned crossing ahead (12dcb95c, default on)

`bot_moveEdgeHull 4` on every bot, but only while the walk follows a planned route with no crossing (hop, drop, pad,
climb) in its next four entries: a walk toward a planned crossing is a run-up to a lip the bot means to leave from.
Build `edgeroute-build-20261005-a`.

- Gates (`gate-20261005-e`): floor 36/36, no deaths; the level-1 and level-2 stand times identical to `gate-20261005-b`
  at every class; level-2 coverage 36/36, tail 13,575; cost p99 21.5 / 18.1 ms (the same build without the probe read
  16.4-21.4 across runs); row 3 misses at class 150 by 0.8 point (the stale library, as before).
- 4v4, 16 paired seeds a map, on against off (`edgeroute-20261005-a`): Kejim team captures -0.13 +- 0.42, pickups
  -0.78 +- 0.70, camper falls -1.43 +- 0.69, base D falls -0.25 +- 0.45, runners' weapon deaths -3.43 +- 1.29;
  Dantooine team captures +0.21 +- 0.29, pickups +0.08, base D falls +0.12 +- 0.15, chase D -0.38 +- 0.40.

Kept. Base D's remaining Kejim falls (about 1.7 a world): 17 of 19 in the on arm follow a walk-only route of one to
four cells that braked (`floor_missing`) just before; the brake now fires in time, and the walk then heads back for a
target past the lip. Suspect, unmeasured: defender goals (block, chase sighting) placed off the floor near the edge.

## Takeoff-window entry searches deferred (`bot_moveWindowDefer`, 60df886b, default 0)

Kejim's slow frames (four default 4v4 worlds, `kejfalls-20261005-a/default`, `.codex-research/tail-20261004-a/window.py`):
takeoff-window straight searches that find nothing are 957 a world and 16.4 s of the 31.1 s of slow-frame bot time;
those that commit, 142 (2.1 s). The cadence repeats in a window were already deferrable; the window's first frame
(`m->state.edge != edge`) ran whole. `bot_moveWindowDefer 1` lets `WalkDeferrable` take that frame too: the walk
deferral's K = 6 prediction, pool and rescue. Build `windefer-build-20261005-a`.

Timing alone (`windefer-20261005-a/timing`, Kejim 4v4 seeds 1-2, one world at a time):

| seed | mean ms off / on | p99 ms off / on | frames over 25 ms off / on |
| --- | --- | --- | --- |
| 1 | 5.47 / 5.34 | 35.5 / 27.2 | 673 / 325 |
| 2 | 5.74 / 4.99 | 40.5 / 25.8 | 722 / 272 |

Outcomes, 16 paired seeds a map, four at a time (`windefer-20261005-a`; frame figures from these worlds: Kejim p99
41.3 -> 28.9, over 25 ms 732 -> 380; Dantooine p99 37.6 -> 35.4, 940 -> 853, its tail being other searches):

| per world | Kejim | Dantooine |
| --- | --- | --- |
| team captures | +0.28 +- 0.42 | -0.33 +- 0.26 |
| runner captures | +0.37 +- 0.40 | +0.31 +- 0.27 |
| camper captures | -0.09 +- 0.21 | **-0.64 +- 0.23** |
| both runners' falls | -1.04 +- 1.13 | +0.93 +- 0.74 |
| defenders' enemy-flag pickups | +0.06 +- 0.36 | **+0.86 +- 0.32** |

Not adopted yet: Kejim's tail roughly halves with no outcome change, but Dantooine's camper captures fall beyond 2.5 se.
