# Surviving the pickup: arrival check, held objective change, path-bound walk speed

Recorded 2026-09-25. The open item "the bot runs on after completing with no
path" ([new-maps record](2026-09-25-new-maps.md)), taken after the planned drop
search ([record](2026-09-25-drop-plan.md)).

## What happens

Not a run-on: the flag is picked up at speed and the bot dies holding it.
Retained worlds, scanned 3 s past the objective: 6 of 2,392 touched a hurt
volume, at two sites, both on respawn-4 approaches - ctf4 `red_out_L3_respawn4`
(the base pit, 2,037 units) and Dantooine's L2/L3 respawn-4 cases (the hurt
water, 534). Two mechanisms:

1. **The arrival is never judged past the flag.** The forecast refuses on
   trigger contact, and a contact at the objective counts as arriving
   (`StraightArrives`), so what follows the pickup is never rolled out. On ctf4
   the pickup came mid-hop at 571 u/s and the hop carried on into the pit.
2. **The objective change hands a flight to the walk.** `Move_Plan` resets all
   movement state when the goal moves; mid-hop, the walk took over in the air,
   found no path, kept its speed and strafed for more (526 to 707 u/s on
   Dantooine), and carried the carrier off the flag platform. With no path the
   walk steers by the navmesh, and it still kept and built speed.

The benchmark shuts a world down at the pickup, so none of this was scored.

## Changes

- `--linger SECONDS` (`movement_bench.py`): a passed trip's world runs on; the
  result's `after` block records death, capture, a flag dropped without a
  capture, and the largest drop (`after_summary`, tested).
- `bot_moveArriveSafe` (default on, owner 2026-09-25; 0 off): an arrival is rolled on
  once more with team flags passed (`Bot2_ForecastFlagPass`), through the
  committed commands and then the knock recovery's coast until the bot stops;
  one that touches a hazard, takes a lethal fall or does not end standing is
  refused `arrives_unsafe` (`[ARRIVE]` line). Under the same cvar the
  objective change is held while a committed sequence is between its launch
  and its last landing (`objective_held`), so the flight the check judged is
  the one flown.
- `bot_moveWalkKeepPath` (default on, owner 2026-09-25; 0 off): only a walk on the
  planned path keeps its speed and air-strafes; off it, it asks for its
  wishspeed. The momentum look-ahead still follows the speed the bot has.

Three versions were withdrawn on evidence (below): refusing continuations that
fall more than `MOVE_FALL_DROP`, refusing those that end off a graph cell, and
gating the momentum look-ahead on the walk's keep.

## Results

Settings as the drop-plan campaigns (items on, planned drops), pinned classes
50 and 150, `--linger 4` (about 20 s of game time watched after each pickup).
Both off: `.codex-research/arrivesafe-20260925-e/` (build `arrivesafe-build-20260925-f`;
the off path is identical on `-g`, 32 of 32 shared ctf4 cases). Both on:
`.codex-research/arrivesafe-20260925-f/` (build `arrivesafe-build-20260925-g`;
`summary.txt`).

| Map | Pairs | Faster | Slower | Sum | Died after pickup, off / on |
| --- | --- | --- | --- | --- | --- |
| Taloraan | 59 | 7 | 3 | -1,550 ms | 0 / 0 |
| Carratos | 52 | 6 | 0 | -10,350 | 0 / 0 |
| Metalorn v2 | 59 | 0 | 18 | +3,325 | 0 / 0 |
| ctf4 | 60 | 0 | 2 | +500 | 0 / 0 |
| Dantooine v3 (L1-2) | 44 | 0 | 0 | 0 | 0 / 0 |
| Kejim | 54 | 4 | 4 | +100 | 0 / 0 |
| **All** | **328** | **17** | **27** | **-7,975 ms** | **0 / 0** |

The deaths live in the old settings. Dantooine respawn-4 L2-3 at class 150
with items off and the old drop search, lingered: **both off 4 of 7 died,
arrival check only 4 of 8, both on 0 of 8** (both-on repeated on builds `-f`
and `-g`, 0 of 8 each). On Dantooine it is the path-bound walk that saves the
carrier; the arrival check and hold are for a committed flight that continues
into a hazard (ctf4).

No failures in any arm; 124 of 124 "dropped" flags in the first scan were
captures, which `after_summary` now separates. Unsafe refusals in the final
arm: 70, all hazards (`unsupported` 256 hurt, 264 hurt and liquid); 243
objective changes held. Metalorn's +3.3 s is 125-250 ms a case: at class 150,
50 refusals of arrivals whose coasting continuation enters hurt water - a
conservative reading, since the live bot steers after the pickup - and at
class 50 the held changes and the path-bound walk.

Withdrawn on the way (`arrivesafe-20260925-c` stopped; `-d` and `-e` kept):
the fall test refused survivable stand drops (Kejim's is 320 units); the
graph-cell test refused 1,186 of 1,260 arrivals that ended standing on floor
the graph has no cell for; and gating the momentum look-ahead on keep made an
off-path walk at 451 u/s look only 96 units ahead - two carriers went over
Carratos's stand ledge on the way home (both arms passed that ledge fast: 0 of
15 died without the gate, 2 of 24 with it).

## Not done

- Defaults: both **on (owner, 2026-09-25)**.
- Parity with the harness on; the gates.
