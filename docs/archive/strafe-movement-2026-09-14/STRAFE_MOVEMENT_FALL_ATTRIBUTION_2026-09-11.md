# The four chasm plunges, attributed (2026-09-11)

Step 1 of the
[branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md#next-work-in-order):
reconcile the native death cause and victim role, then trace the two actual
blue normal-speed falls for ownership, carrying, contact and preceding
commands, keeping the red-1x-r1 5:59 repeater-splash descent as a separate
safety case.

**No native world was rerun and no frozen report was changed.** This reads the
retained tapes and engine logs of the twelve worlds already under
`.codex-research/pkgD-step4-defended`, the 180 seeded solo worlds of
`pkgD-step4-20260911` and `pkgD-control-20260911`, and the sources in the
checkout those worlds were built from. Nothing is installed; the installed
build remains `strafe-build-reviewready-20260910-b` (`525f1b50`), and K07 is
not qualified.

## Kill credit is not causation, and the engine says so in a comment

The reconciliation the plan asked for turns on three paths in this tree.

1. A `MOD_FALLING` death is **a map volume, not a long drop**. `g_trigger.c`
   sets `ps.fallingToDeath` when a client touches a `damage == -1` trigger
   (`codemp/game/g_trigger.c:1389`) and `g_active.c:2764` kills the client once
   that stamp is `FALL_FADE_TIME` = 3000 ms old (`q_shared.h:1062`).
2. Any damage with knockback sets `ps.otherKiller` for 5000 ms
   (`g_combat.c:4723`), and `g_active.c:2792` clears that credit as soon as the
   victim is grounded again past a 100 ms debounce.
3. The fall trigger **extends** a live credit to 20 s, with the engine's own
   comment — *"we're as good as dead, so if someone pushed us into this then
   remember them"* (`g_trigger.c:1385`).

So `Bot2Enemy1_v2 killed Runner_v2 by MOD_FALLING` means the runner entered the
death volume within five seconds of an opponent's hit and did not touch ground
in between. It does **not** establish that the hit is what took the runner off
the floor; the opponent's own falls in these same worlds are credited to
`<world>` (`red-1x-r1/base/games.log:93`) precisely because nothing had hit
them. Which of the two it is has to be measured, and below it is.

## Four plunges, and only three of them are engine falls

A plunge here is a maximal run of runner samples below z 1300 — chosen because
the lowest *grounded* height anywhere in these worlds other than the chasm floor
is 1368, and the chasm floor is a single surface at **z 32.1**.

| World | rate | departure | speed | owner at departure | carrying | ends | engine record |
|---|---|---|---|---|---|---|---|
| blue-1x-r1 | 1x | (214, −610, 1891) grounded | 241 u/s | `supported` | no | z 32.1, killed 2525 ms later | `MOD_FALLING`, credited to Bot2Enemy1_v2 (`games.log:126`) |
| blue-1x-r3 | 1x | (485, 105, 1808) grounded | 250 u/s | `supported` | no | z 32.1, killed 2400 ms later | `MOD_FALLING`, credited to Bot2Enemy1_v2 (`games.log:60`) |
| red-1x-r1 | 1x | ≈(930…957, 1700…1754, 1809) mid-hop | ≈360 u/s | `supported` | no | killed at z 1089 **during** the plunge; the corpse falls on through z 388 | `MOD_REPEATER_ALT_SPLASH` (`games.log:90`) |
| blue-5x-r1 | 5x | (624, 1391, 1873) mid-hop | 426 u/s | `fastreturn` until the hit, `localnav` after | **yes** | z 32.1, killed 2475 ms later | `MOD_FALLING`, credited to Bot2Enemy1_v2 (`games.log:142`) |

Three of the four are the three engine runner falls the step-4 review named.
The fourth, red-1x-r1 at 5:59, is the death the review correctly reclassified as
repeater splash — and the movement fact the reclassification could not see is
that **it was already in an unrecoverable plunge into the same chasm when the
splash killed it**: the last four samples fall 1808 → 1089 at −1214 u/s, and the
corpse's next two samples are z 763 and z 388, still falling. Keeping it as a
separate *means-of-death* case is right; dropping it from the *plunge* count
would not be.

Eight `MOD_FALLING` lines exist across the twelve worlds: three runners and five
opponents, exactly as the step-4 review recorded.

## Two were pushed, two steered off

`G_Damage`'s knockback adds velocity along the damage direction, so the
departure question is answerable from the tape. Per damage event, the runner's
horizontal velocity change across the interval that contains it, against the
direction pointing away from the credited opponent:

| Case | damage | opponent range | speed | heading turn | \|Δv\| | impulse vs. away-from-opponent | `PMF_TIME_KNOCKBACK` |
|---|---|---|---|---|---|---|---|
| blue-1x-r1 | 43 | **199–213 u** | 241 → 243 | **96.5°** | **361** | **5.5–10.5°** | not sampled |
| blue-5x-r1 | 26 | **218–431 u** | 426 → **678** | 3.2° | **254** | **2.9–4.6°** | **set** |
| blue-1x-r3 | 47 | **514–713 u** | 250 → 309 | 22.8° | 125 | **103°** | not sampled |
| red-1x-r1 | 53 | 195 u | 410 → 260 | 38.3° | 262 | 3.5–37° | not sampled |

- **blue-1x-r1 was knocked off.** The velocity keeps its magnitude and turns
  96.5°, the 361 u/s change points within about ten degrees of directly away
  from an opponent 200 units off, and the runner was grounded at the start of
  the interval. It never touched floor again.
- **blue-5x-r1 was knocked off from behind**, on the return lane, carrying the
  flag. `PMF_TIME_KNOCKBACK` is set in the sample itself, the chaser is 218–431
  units directly behind, and the impulse adds **252 u/s** along the runner's own
  heading (turn 3.2°): 426 u/s became 678 u/s and the hop overshot into the
  chasm.
- **blue-1x-r3 steered off.** The nearest opponent is 514–713 units away, the
  impulse is small and points 103° from away-from-opponent, and the heading
  before and after the hit agree to 23°: the runner continued the line it was
  already on, from (485, 105) out over the edge at (444, 36). The 47-point hit
  is what bought the opponent its kill credit 4.6 s later; it is not what took
  the runner off the floor.
- **red-1x-r1 steered off.** Its hit lands 1.75 s *before* the plunge, and
  `PMF_TIME_LAND` at (872, 1535) shows a ground contact after it, so the hit
  cannot be the departure. What follows is an ordinary `supported` hop chain at
  250–480 u/s along +x+y — (622, 1280) → (743, 1395) → (872, 1535) → (957, 1754)
  — whose last hop has no floor under it. Free fall begins by (957, 1754) with
  vz −319 and never recovers.

Both self-steered departures are at **normal speed**, on the **outbound fetch
leg** (`objective` 0, goal (152, −1160) for blue and (2464, 5312) for red),
**not carrying**, with the strafe route **inactive** (`route_active` 0
throughout) and the `supported` executor owning the movement. Neither is a lane
or branch geometry failure: no branch was open, no route index was live, and the
authored catalog is not on either line.

## Nothing after the impulse is a movement decision

All three completed plunges land **alive** on the chasm floor — 47, 12 and 64
health — and then stand still with zero input for 2.4–2.5 s until the engine's
timer kills them. That stillness is not a missed recovery.

- The 3000 ms clock had already started **mid-fall**, not at landing. Working
  back from each death through `FALL_FADE_TIME` and one 25 ms frame puts the
  trigger touch at z ≈ 670–705 (blue-1x-r1, from z 704.6 at −1387 u/s),
  z ≈ 722 (blue-1x-r3) and z ≈ 725 (blue-5x-r1) — one boundary near **z 700**,
  about 1100 units below the floor the runner left. Past it the outcome is
  fixed.
- `bg_pmove.c:11185` zeroes `forwardmove`, `rightmove`, `upmove` and `buttons`
  for any client with `fallingToDeath` set, so no command issued after the
  boundary can matter.
- `ai_bot2.c:401`'s `fallingToDeath → G_Kill` shortcut never runs in these
  worlds. With `bot_movementOnly 2` the runner returns from the movement-only
  branch at `ai_bot2.c:266`, and `ai_bot2.c:251` returns before anything else
  for every client that is not a play dispatch, so the opponents run stock AI.
  Every fall-to-death here is the engine's timer.

The whole movement-side opportunity is therefore the **250 ms interval that
contains the departure**, and for the two pushes there is no opportunity at all:
the impulse arrives while the runner is already at the edge.

## Denominators

| Population | worlds | plunges | lowest grounded z |
|---|---|---|---|
| Seeded solo, control arm | 60 | **0** | 1808.0 |
| Seeded solo, sources-only and candidate arms | 120 | **0** | 1808.0 |
| Defended, normal speed | 6 | **3** | 32.1 |
| Defended, timescale 5 | 6 | **1** | 32.1 |

In 180 seeded solo worlds no runner sample is ever below 1808. The plunge family
belongs to full-cycle defended play, where the runner walks the outbound fetch
leg under `supported`, and at normal speed it costs **three plunges per hour of
game time** (3 in 6 × 600 s).

It is not rare traffic. Counting approach episodes within 200 units of the three
departure sites and their mirror images — the map mirrors through
(2616 − x, 4152 − y), verified on both flag stands and both capture corners —
each neighbourhood is entered **35 to 62 times per world**, about 1900 passes
across the twelve worlds, for those four plunges.

## What this changes

- **T6's falling-death clause stays failed**, and the reason is now named.
  **Two of the three normal-speed plunges are self-steered** `supported` steps
  with no floor under the last one, and are movement's to fix; the third is
  opponent knockback at a chasm edge, for which no movement remedy exists after
  the impulse. The fourth plunge, also knockback, is the timescale-5 carrying
  case. Of the **two normal-speed engine falling deaths**, one is knockback
  (blue-1x-r1) and one is self-steered (blue-1x-r3); the third self-steered
  plunge was ended by a weapon before the fall timer could finish it.
- **The remaining movement defect is in the supported traveler, not the branch
  package.** Route inactive, no branch open, not carrying, outbound leg. The
  branch floor gate BR-A learned ("nearness to measured play is not evidence of
  standable floor") is the right rule and it does not apply here, because these
  departures are not on an authored line.
- **The one plunge that touched the fast-return lane is a ×5 carrying case**
  where a rear impulse added 252 u/s to a 426 u/s hop. Whether a lane hop should
  survive a rear knockback of that size is a real question; it is not one of
  T6's normal-speed clauses, and one sample cannot size it.

## What is not established

- **Which weapon landed each hit.** `games.log` records kills, not hits. The
  four are reported by damage (26–53) and by the opponent's range, nothing more.
- **Whether a floored alternative exists at either self-steered site.** The
  tape proves the last hop had no floor; it cannot prove a nearby line has one.
  Native floor/clearance probes at (485, 105) → (444, 36) and at
  (930…957, 1700…1754) are the plan's tool for that, and absent grounded
  recordings are not proof of absent floor.
- **Why `supported` chose those hops.** The navmesh link set at both sites is
  unread here; the tape shows the goal (the enemy flag stand) unchanged across
  every sample, so the departure is a local step choice, not a retarget.
- **Ground contacts between samples.** The tape is 4 Hz plus transitions, and
  `PMF_TIME_LAND` at an airborne sample proves contacts it did not record. Every
  departure above is therefore bracketed, not pinpointed; the 40 Hz momentum
  trace was not enabled in the defended campaign.
- **That any repair works.** Non-recurrence across a handful of defended worlds
  would not establish a cure at three plunges per hour; a fix needs the
  departure geometry to change, measured at the site.

## Evidence and how to reproduce

Under `.codex-research/pkgD-fallattrib-20260911`, all derived from the retained
worlds with no native run:

| Artifact | What it holds |
|---|---|
| `fall_attribution.py`, `<world>-attrib.json` (12) | every runner death in each defended world with its 6.5 s window: ownership, carrying, route, commands, `pm_flags`, health steps and the nearest opponent joined by nearest sample time |
| `chasm_entries.py`, `defended-plunges.json`, `seeded-plunges.json`, `control-plunges.json` | every plunge below z 1300 in the twelve defended and 180 seeded worlds, with departure context, the engine kill that matches it, and the grounded-floor distribution the z 1300 threshold rests on |
| `impulse.py`, `plunge-impulse.json` | the velocity change across each damage interval against away-from-opponent, for both bracketing opponent samples |
| `edge_traffic.py`, `edge-traffic.json` | approach episodes within 200 units of the three departure sites and their mirrors, per world |
| `show_death.py` | prints one death's window as a table |

```bash
python .codex-research/pkgD-fallattrib-20260911/chasm_entries.py .codex-research/pkgD-step4-defended/*-r?
python .codex-research/pkgD-fallattrib-20260911/show_death.py .codex-research/pkgD-fallattrib-20260911/blue-1x-r1-attrib.json 592900
```
