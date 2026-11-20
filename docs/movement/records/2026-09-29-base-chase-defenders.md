# Base D and chase D, 2026-09-29

Owner, 2026-09-29: bot against bot comes down to arithmetic, meaning
seconds of quality shooting and the damage done in them. Chasing along the
carrier's own route at the same speed does not close. All of this is
before the offense uses protect, push or its own tools. The order: a basic
chase D and a basic base D.

Build `roles-build-20260929-c`. Worlds and `analyse.py` are in
`.codex-research/roles-20260929-a/`.

## What they do (`bot2_defender.c`)

Two declared defenders can now play at once, each with its own state:
`Defender_v2` in slot 0 and `Chaser_v2` in slot 1. The bench adds them
with `--defender-roles solo|base|chase|base,chase`. Both use the same aim
(the competent style), the same weapon, and the same pulls and throws.

- **Base D** (`Defender_v2`, `bot_defenderRole 1`):
  - It guards its posts (`--defender-posts 2v2`: entry spot, stand,
    forward).
  - After a steal it fights the carrier only while the carrier was last
    seen within `bot_defenderLeash` (default 1,500) of its own stand, then
    goes back to its posts. Base players rarely cross the map
    ([pilot](2026-09-27-position-pilot.md): enemy base and stand at most
    6%).
- **Chase D** (`Chaser_v2`, `adddefender <team> chase`):
  - While its flag is home it holds the 2v2 forward post toward mid
    (`chase_posts` in `posts.json`).
  - After a steal it intercepts. It takes the carrier's graph route from
    where the carrier was last seen to the carrier's own stand (map
    knowledge). Its goal is the first cell on that route it reaches first:
    its straight-line distance × 1.3 at `bot_chaserSpeed` (default 450),
    plus 0.3 s, against the route distance at the carrier's perceived
    speed (at least 500). Only points within 5 s of the chaser count.
  - It recomputes every 500 ms (`INTERCEPT` lines) and tails the carrier
    when no point works.
- **Solo** keeps the single-defender behaviour of the earlier records.

Smoke fix: the first intercept assumed a carrier out of sight moved at
400 u/s. That sent the chaser to the carrier's own stand, 7,000 u away.
The bounds above fixed it.

## Results

Kejim, classes 50 and 150. Pickups (48 worlds) and returns (36). Rocket,
competent aim at human error with the reach gate, reaction 250 ms, Speed
and strafing, 2v2 posts, carrier pulls at 350-700 u. Retired pin-miss
worlds excluded.

| Pickups | Attackers stopped before the grab | Carriers killed | Kill after the grab, median |
|---|---|---|---|
| solo | 1 | 2 | 5.0 s |
| base D | 1 | 2 | 5.0 s |
| chase D | 5 | 3 | 7.9 s |
| base D + chase D | 5 | 9 | 7.6 s |

| Returns | Carriers killed | Kill after the start of the carry, median |
|---|---|---|
| solo | 1 | 0.2 s |
| base D | 1 | 1.4 s |
| chase D | 0 | - |
| base D + chase D | 3 | 5.5 s |

**Per role.**
- **Base D:** 89-108 shots, 53-66 hits on pickups. During the carry it
  gets within a median 56-69 u of the carrier.
- **Chase D:**
  - it fires little: 23-26 shots, 6-13 hits;
  - 82-92 intercepts on pickups, 68-69 on returns;
  - its closest approach to the carrier is a median 410-520 u.

**Reading.**
- **The chaser's forward post stops attackers** before they reach the flag
  (5 against 1). It stands on the respawn-4 route through mid.
- **Together they kill more carriers than either alone:** 9 against 2-3 on
  pickups, 3 against 0-1 on returns. The kills come later (5.5-7.6 s into
  the carry), at the intercept: base D wounds the carrier and chase D
  finishes it on the way home.
- **The intercept gets the chaser ahead of the carrier, but not close.**
  Its closest approach stays 400-500 u, and it shoots little. The
  intercept point is where the routes cross, not a firing position.

## Open

- Chase D's firing position: stand off the carrier's route at rocket range
  with a clear line, instead of on it.
- The offense's answers (protect, absorb, push/pull, Speed choices) are not
  modelled. Every number here is against a runner that only runs.
