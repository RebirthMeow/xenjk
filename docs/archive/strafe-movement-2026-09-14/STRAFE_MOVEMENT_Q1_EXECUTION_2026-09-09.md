# Q1: recovery execution under moving pressure and the fall diagnosis

Executed 2026-09-09 UTC from the R9 checkpoint `aabdb964` on local `master`,
following the [Q1 package](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE_HISTORY_2026-09-12.md#q1--establish-recovery-execution-and-diagnose-the-fall)
of the post-R9 handoff. The active [plan](STRAFE_MOVEMENT_PROJECT_PLAN.md) and
acceptance gates are unchanged. Nothing was installed into GameData; all worlds
were private hidden localhost dedicated servers. No timing claim is made here.

## Summary

- The bounded recovery continuation (`corner_continue`) now has native
  execution evidence under a following body: 8..10 continuations per
  eight-case collection across six collections, most followed within two
  seconds by displacement and a smaller objective distance while the body
  stayed 30..66 units away, with aborts on re-obstruction, clear handbacks and
  three-step exhaustion. The static and slow-body cases hand back to ordinary
  movement after one step as before. Under sustained pursuit at running speed
  the outcome varies between runs of the same case: 0..2 confinements of up to
  13.25 s and 44..56 % travel idle, never a capture in the window.
- The R8 `adapter-s4` death is still not reproduced. The retained R9 traces
  contain an airborne hull contact between clients 1 and 4 at elapsed 9.6 s
  that removes client 1's entire lateral velocity mid-flight; the map's single
  instant-kill pit trigger tops out at z=704 and produces `MOD_FALLING` three
  seconds after entry, which places that world's entry near elapsed 11.4 s,
  about 1.75 s after the same contact. The death remains **unclassified as to
  avoidability**; this is the leading evidence-backed candidate mechanism.
- The new diagnostic trace columns captured a **different, complete fall** on
  their first world: after a recovery arrival and a rejected supported
  preparation, the cached-route follower rejoined route 19 at its takeoff step
  84 from about 320 u/s where the guide carries 1170 u/s, the low arc struck the
  face of the next platform, and the client entered the pit trigger. That is a
  demonstrated controller defect. The rejoin policy now declines a rejoin whose
  first takeoff ahead needs strafe momentum that neither the runner nor ground
  running can supply when the recorded flight crosses a gap, deep drop or
  climb, and never rejoins across missing floor (four floor samples between the runner and the rejoin point); a first momentum-only version broke 17/26 temporary-body cases and three further variants (an index-based gap rule, floor samples that refused a body under a sample, and judging the takeoff from the rejoin index instead of the kernel window) two more each; all replaced. Verification worlds and regressions are recorded below.
- K07 remains unqualified. Q2 (independent references), Q3 (integrated
  quality), Q4 (cost) and Q5 (final freeze) remain as ordered.

## Source changes

| File | Change |
|---|---|
| `codemp/game/bot2_corner.c` | Cheats-only pursuing body proxy: with `block` and `bot_cornerShadowSpeed` 1..400 (`bot_cornerShadowGap` 24..96, default 36) the proxy translates each frame at most speed×dt toward a point `gap` units ahead of the runner's horizontal velocity (facing yaw below 40 u/s), under ordinary player-hull collision with one slide, a native-step floor check, a 512-unit leash and a hold while the runner is dead. Never teleported or removed; no AI, resources, damage or pushing. Prints `SHADOW_START`, a `SHADOW` sample every 250 ms and the first `SHADOW_CONTACT`. Static mode is unchanged apart from `static_body_proxy=0/1` in the `BLOCKER` line. |
| `codemp/game/ai_bot2_localnav.c` | Print-only: `corner_step` and `corner_blocked` telemetry now carry `level=`. No policy change; the R9 recovery code is otherwise byte-identical. |
| `codemp/game/bot2_movecost.c` | Trace mode appends `pre_falling,post_falling,owner` (native `fallingToDeath` stamps around movement and the frame's top-level movement owner). Older recordings stay readable. |
| `codemp/game/ai_bot2_route.c` | `ResumeRoute` judges, for each rejoin candidate, the first takeoff in the kernel's own takeoff window (from three steps behind the candidate, since an already-reached takeoff retains its input): when neither the actual momentum nor Speed-boosted ground running (`basespeed×1.7`) reaches 65 % of the recorded launch speed **and** the recorded flight (through chained takeoffs) crosses a step without a prepared floor or a floor more than 128 units below the takeoff, or lands more than 36 units higher, the candidate is refused, and so is everything past it when that takeoff lies at or ahead of the candidate (a takeoff already behind the candidate only skips it). A rejoin is also refused when any of four floor samples between the runner and the rejoin point finds no world floor within a native step above and 128 units below the runner (no rejoin across missing floor); a body or mover under a sample counts as support, since the temporary M2 body stands on the guide's own steps. Bounded `resume_refused` telemetry (`reason=runup`/`gap`). The frozen strafe kernel and route data are untouched. |
| `tools/bot_test_runner.py` | `rcon_send(collect=…)` gathers every reply packet; the server flushes about one kilobyte per packet and the corner setup reply now exceeds one. Default behavior unchanged. |
| `tools/strafe_play_smoke.py` | `--blocker-shadow-speed`, `--blocker-shadow-gap`; passes the cvars, records `blocker_shadow` in provenance/intervention/summary, confirms `SHADOW_START` with the declared values and refuses an unrequested one. |
| `tools/strafe_corner_progress_audit.py` | `native_events`: recovery/refusal/body-proxy prints inside the corner window from the RCON reply and server log, body versus world floor refusals by blocker entity, episode grouping, per-continuation goal distance/displacement at +2 s/+5 s and nearest proxy distance. |
| `tools/strafe_cost_trace_audit.py` | Optional Q1 columns, `pit_entries` (falling stamps with the preceding airborne episode) and `airborne_contacts` (airborne frames whose lateral velocity changes by ≥150 u/s, attributed to any hull-touching client or to world/unknown). |
| `tools/strafe_route_resume_check.py` (new) | Mocked Win32 check of the extracted rejoin policy. |
| `tools/strafe_shadow_summary.py` (new) | Input-bound summary over corner audits; no pass gate. |

Mocked checks on the final sources: `strafe-recovery-episode-check-20260909-q1a`
(pass), `localnav-reentry-q1-5d2c1b7e9a3f4c8d` (pass, 2900 handoffs) and
`strafe-route-resume-check-20260909-l` (pass; `-b`, `-f`, `-h`, `-i` and `-k` bound superseded gates or expectations). Source hashes are in each check's `result.json`.

Builds (each 1102 source inputs, sources frozen during collection):

| Build | Contents | Used for |
|---|---|---|
| `strafe-build-q1-shadow-3f5c9a2e7b1d4e0f9a6c8b7d5e4f3a21` | corner shadow, localnav level stamps | first two shadow collections |
| `strafe-build-q1-falltrace-8c1e4d2a6b7f4e3d9a5c1b0e2f6d7a84` | plus trace columns | trace world `sample-1` |
| `strafe-build-q1-resume-2b9d7e4f1a6c4d3e8f0b5a7c9e1d2f36` | plus the superseded momentum-only rejoin gate | trace world `sample-2`, failed regression attempt, shadow collection `-final` |
| `strafe-build-q1-final-7e3a9c1d5b2f4a8e6d0c4b9a1f7e2d53` | intermediate ground-maximum rule, never measured | none (retained, unused) |
| `strafe-build-q1-final-9a4b2c7d1e6f4a3b8c5d0e9f2a1b7c64` | geometry-aware takeoff rule with an index-based gap rule | regressions `-b` (24/26 body), shadow collection `-final2`, trace world `sample-3` |
| `strafe-build-q1-final-c5d8e1f2a3b64c7d9e0f1a2b3c4d5e61` | floor-sample gap rule refusing bodies under a sample | regressions `-c` (24/26 body), shadow collection `-final3`, trace world `sample-4` |
| `strafe-build-q1-final-d4e7f0a1b2c34d5e6f7a8b9c0d1e2f38` | floor samples with bodies counted as support; takeoff judged from the rejoin index | regressions `-d` (24/26 body), shadow collection `-final4` |
| `strafe-build-q1-final-e8f1a2b3c4d54e6f7a8b9c0d1e2f3a49` | takeoff judged from the kernel window (three steps behind the rejoin); final Q1 revision | regressions `-e`, shadow collection `-final5`, trace world `sample-5` |

## Pursuing-body protocol

Declared before collection in `.codex-research/strafe-q1-shadow-20260909/plan.json`:
the three R9 static placements that actually blocked the runner (red west and
south at (2550,5582,1913) yaw −110; blue north at (53,−1450,1913) yaw 70),
force 0, carrying, no Speed, candidate arms (`--supported-corners 2`, flight
artifact, adapter, guard, supported trace), 30 game seconds at 5×; proxy speeds
0 (control), 60, 120 and 250 u/s with gap 36. Eight cases, two worlds at once.

The first collection retained four invalid attempts (`red-west-s60`,
`red-west-s120`, `blue-north-s120`, `blue-north-s250`): the RCON setup reply
was cut at one packet, the runner could not see the START marker and quit the
already-mutated world. The reader was corrected and those four cases were
collected again unchanged in `strafe-q1-shadow-20260909-b`. All eight cases are
complete with clean shutdown; the input-bound summary is
`.codex-research/strafe-q1-analysis-20260909/shadow-summary.json`.

| Case | Episodes (steps) | `corner_continue` | Proxy ≤96 u (of 120 samples) | Confinement | Travel idle % | Capture |
|---|---|---|---|---|---|---|
| red-west-static | 1 (1) | 0 | – | none | 10.9 | 30.7 s |
| blue-north-static | 1 (1) | 0 | – | none | 10.9 | 29.5 s |
| red-west-s60 | 1 (1) | 0 | 17 | none | 11.7 | 32.3 s |
| red-west-s120 | 1 (1) | 0 | 24 | none | 14.3 | after window |
| red-west-s250 | 2 (3, 1) | 2 | 24 | none | 10.9 | after window |
| red-south-s250 | 3 (1, 3, 1) | 2 | 35 | none | 19.3 | after window |
| blue-north-s120 | 1 (1) | 0 | 19 | none | 12.6 | 30.2 s |
| blue-north-s250 | 8 (1,3,1,1,2,1,1,3) | 5 | 115 | one, 6.75 s at 23.6 s | 44.5 | none (still carrying at 36 s) |

Observed natively:

- **Continuation with the body present.** Nine `corner_continue` prints; seven
  are followed within two seconds by displacement of 109..251 units and a
  smaller objective distance while the nearest proxy sample is 38..66 units
  away (the two late blue continuations at 29.8/30.3 s end the window before a
  +2 s sample exists). Both red 250 cases run full three-step episodes and then
  leave the proxy behind; it reaches its leash and holds.
- **Current-state validation.** `red-south-s250` aborts its first step at
  sample 3 with `floor_startsolid` on the blocker entity when the proxy slides
  into the corridor; `red-west-s250` and `blue-north-s250` abort one step each
  with `direct_blocked`. Every abort is followed by a new episode.
- **Clear handback.** All other episodes end by arrival with the ordinary route
  resuming, including the slow 60 u/s case and both 120 u/s cases; no timeout
  end occurred natively (timeout remains mocked coverage).
- **Step exhaustion.** Four episodes reach the three-step bound and end by
  arrival; none exceeds three steps or six seconds.
- **Sustained pursuit.** `blue-north-s250` keeps the proxy within 96 units for
  115 of 120 samples with 198 recorded contacts. The runner never dies and
  advances about 1070 units north with eight episodes, but records one 6.75 s
  confinement and 44.5 % travel idle, and does not capture within 36 s (the
  static blue control captures at 29.5 s). This is the honest cost of a body
  that always stands in front at base running speed; it is not a stall
  classification for Q3, and it is not escape qualification either.
- Objective change during an episode and airborne/lifecycle interruption were
  not exercised by these cases; they remain covered by the mocked episode
  check and the retained R9 blue fifth-step objective change.

Limits: the proxy has no combat, jumping or path planning and cannot follow
around geometry beyond one slide per frame; sampled telemetry cannot prove
unsampled continuity; no pass gate is applied.

## Fall diagnosis

### Input-bound evidence for the R8 death

- The map `mp/ctf_kejim` has exactly one `trigger_hurt`: x −768..3392,
  y −1592..5752, z 8..704, `dmg −1`, i.e. the pit. With `dmg −1` the trigger
  stamps `ps.fallingToDeath` on entry and the client dies of `MOD_FALLING`
  after `FALL_FADE_TIME` (3000 ms). The `adapter-s4` kill at elapsed 14400 ms
  therefore implies pit entry near elapsed 11400 ms.
- All four R9 trace worlds (identical declared setup, no death) contain the
  same event: client 1's released route flight touches client 4 hull to hull at
  level 23600 (elapsed 9600), lateral velocity changing from (−523,−757) to
  (−517,+6) in one frame while client 4 jumps up from the landing area; client
  1 then drops along the platform face and lands on z=1368 with 13 damage.
  Client 3 repeats the same contact with client 4 at 23975. The re-audit is
  `.codex-research/strafe-q1-analysis-20260909/` (`r9-sample1` contacts).
- A pure drop from that contact height to z=704 takes about 1.7 s, which is
  consistent with the implied entry time. The `adapter-s4` world recorded no
  positions, so whether its deflection missed the z=1368 floor cannot be shown.
  **Classification: unclassified as to avoidability; candidate mechanism is a
  mid-air body contact during a released route flight.** The fixture's +150 Y
  impulse at 2525 ms precedes this by seven seconds and is not the proximate
  cause; no rocket or other damage source exists in this workload.

### Captured fall on the instrumented build (`sample-1`)

`.codex-research/strafe-q1-falltrace-20260909/sample-1` (build `falltrace`,
identical declared setup to the R9 trace worlds) records a complete pit entry:

| Elapsed | Event |
|---|---|
| 17150 | client 1 recovery `corner_step_end` arrival at (391,−155,1808); supported acquisition; preparation rejected on a body sample (entity 3) |
| ~17200 | `[STRAFEROUTE] resume route=19 index=84 distance=65.7`, owner `route`, speed ≈241 u/s |
| 17550 | takeoff from (386,−172,1808) at ≈320 u/s; route 19 step 84 carries (−378,−1110) ≈1170 u/s and step 86 is a second takeoff onto a higher platform |
| 18525..18600 | apex 1933 passed; lateral velocity zeroed against the platform face at (271,−689,≈1860), no client touching |
| 18800 | owner `localnav` (`nav_reentry gap_ms=9300`, airborne, stale waypoint) |
| 19325 | second wall contact at (254,−753,1364) |
| 19875 | `post_falling=33675` at (207,−753,715): pit entry; death would follow at elapsed 22875, after the 20 s recording |

The kernel's rejoin gate clears run-up at `min(300, 0.65×reference)` and the
takeoff at the current index has no speed gate, so a rejoin at a fast takeoff
from near rest jumps almost immediately. This is a controller defect. The
correction lives in the follower's rejoin selection (see source changes); it
does not touch the frozen kernel or any route data.

### Verification (`sample-2`, regressions)

`sample-2` repeats the declared trace world on the momentum-only build
`…2b9d…` (`plan-2.json`). All eight clients stay alive for all 800 frames; the audit
finds no pit entry, no death and no discontinuity, and provenance matches. The
rejoin gate fired four times for client 6 (route 2, takeoff index 29, recorded
974 u/s, actual 0..21 u/s: a rest rejoin at a fast takeoff, the same class as
the captured fall) and never for client 1, which reached the higher platform
under supported ownership instead. The two worlds diverge before either event
(client 2's pickup is at 12625 versus 12600 ms), so the hypothesis that the
identical step-84 rejoin would be refused could not be tested at the same
instant; the gate's behavior is bound by the mocked check and by client 6's
refusals, and no fall occurred in this world. Both worlds again contain the
client 1/client 4 airborne contact (elapsed 9475 ms) that the R9 traces show.

`sample-3` (build `…9a4b…`, `plan-3.json`) again keeps all eight clients alive
with no pit entry, no death and no discontinuity, and shows the gate acting on
the fatal case class: client 1 asks to rejoin route 19 at indices 78, 79 and 84
ahead of takeoff 84 (recorded 1173 u/s) at 425 u/s and then from rest, and every
attempt prints `resume_refused reason=runup`; the client continues under
supported/ground movement. The client 1/client 4 airborne contact recurs at
elapsed 9500 ms. `sample-4` (build `…c5d8…`, `plan-4.json`) repeats the picture: all eight
clients alive, no pit entry, no death, no discontinuity; client 1 is refused nine
times at route 19 index 84 (`reason=runup`) and client 6 four times at route 2
index 29 by the first floor sample (`reason=gap`), and the client 1/client 4
contact recurs at 9475 ms. `sample-5` (final build `…e8f1…`, `plan-5.json`): all eight clients alive,
no pit entry, no death, no discontinuity, provenance matching and the Q1 columns
present; client 1 is refused eight times at route 19 index 84 (`reason=runup`)
and the client 1/client 4 contact recurs at 9475 ms. Five trace worlds on four
builds have now captured one pit entry (before the gate) and none after it;
that is not a statistical claim, and the R8 death itself is unchanged.

Regressions (30 adapter-armed resource cases through `strafe-r9-resource-batch-two-workers.py`
and `strafe_supported_report.py`; 26 frozen temporary-body cases through
`strafe_disturbance_runner.py`, evaluation partition, two workers, accelerated):

| Collection | Build | Resources | Temporary body | Note |
|---|---|---|---|---|
| `strafe-q1-regression-20260909` | `…2b9d…` momentum-only gate | 30/30 pass | 9/26 pass, 17 timeouts with no jump | retained failed attempt: stand-start rejoins at 731..800 u/s takeoffs refused (`resume_refused reason=runup`); routes 6/2 `launch=800/731` |
| `strafe-q1-regression-20260909-b` | `…9a4b…` geometry-aware takeoff rule, index-based gap rule | 30/30 pass | 24/26 pass; `blue_respawn_4`, `red_respawn_4` time out | retained failed attempt: the runner stood at a ledge edge nearest an airborne guide step over the drop and the admissible rejoin (index 2, behind it on the same floor) was refused as `reason=gap`; the R9 pass resumed at index 3 there |
| `strafe-q1-regression-20260909-c` | `…c5d8…` floor samples refusing bodies | 30/30 pass | 24/26 pass; the same two time out | retained failed attempt: sample 4 at the rejoin point hit the temporary body (`hit=132 fraction=0.123 reason=gap`) for its 700 ms; meanwhile ground movement (localnav, no route) ran the runner about 800 units south to (3215,1608) where it stood still for 17 s, a ground-fallback dead end the route had always masked |
| `strafe-q1-regression-20260909-d` | `…d4e7…` bodies counted as support, takeoff judged from the rejoin index | 30/30 pass | 24/26 pass; the same two time out | retained failed attempt: the rejoin at index 3 was judged against takeoff 8 (`launch=656`, needing 426.4 u/s against the 425 u/s ground maximum, over a climb) although the kernel executes the just-passed takeoff 2 first and reaches 8 with flight momentum; the R9 pass did exactly that |
| `strafe-q1-regression-20260909-e` | `…e8f1…` takeoff judged from the kernel window (final) | 30/30 pass | 26/26 pass; `blue_respawn_4`/`red_respawn_4` resume at index 3 (59.9/59.6 units) as in R9 | retained regressions for the final revision |

The R9 baseline for both suites is 30/30 and 26/26.

### Final-revision pursuing-body collection and variance

`.codex-research/strafe-q1-shadow-20260909-final5` repeats all eight declared
cases on the final Q1 revision `…e8f1…` (summary `shadow-summary-final5.json`):
8/8 complete, 25 episodes, 10 `corner_continue` events, eight of them followed
within two seconds by 51..251 units of displacement and a smaller objective
distance with the proxy 30..39 units away; ten aborts (`floor_startsolid`,
`direct_blocked`, `step_cross_blocked`) each followed by a new episode, fifteen
arrivals, four three-step episodes. The static, 60 u/s and blue 120 u/s cases
capture inside the window and the red 250 cases after it. In this run the
120 u/s red proxy kept pace for the whole window and the runner recorded a
9.75 s confinement with 48.7 % travel idle; the blue 250 case recorded a
13.25 s confinement with 45.4 % travel idle and again no capture. No
`resume_refused` print occurs in these solo worlds.

The same eight cases were collected on every intermediate build. The
continuation branch executes in every collection; the sustained-pursuit
outcome varies between runs of the same declared case, which is itself part
of the measured limit (columns: continuations / confinement episodes / travel idle):

| Collection | Build | `corner_continue` | with +2 s goal progress, proxy ≤96 u | red-west-s120 | red-west-s250 | red-south-s250 | blue-north-s250 |
|---|---|---|---|---|---|---|---|
| first (shadow + -b) | …3f5c… shadow build | 9 | 7 | 0 / none / 14.3 % | 2 / none / 10.9 % | 2 / none / 19.3 % | 5 / 6.75 s / 44.5 % |
| -final | …2b9d… momentum-only gate | 8 | 8 | 0 / none / 13.4 % | 2 / none / 10.9 % | 2 / none / 12.6 % | 4 / none / 43.7 % |
| -final2 | …9a4b… index gap rule | 9 | 7 | 0 / none / 14.3 % | 2 / none / 19.3 % | 2 / none / 21.0 % | 5 / 6.75 s / 44.5 % |
| -final3 | …c5d8… floor samples refusing bodies | 8 | 8 | 0 / none / 14.3 % | 2 / none / 19.3 % | 2 / none / 10.9 % | 4 / none / 43.7 % |
| -final4 | …d4e7… rejoin-index takeoff | 10 | 9 | 2 / none / 18.5 % | 2 / none / 12.6 % | 2 / none / 10.9 % | 4 / 9.75 s,5.75 s / 55.5 % |
| -final5 | …e8f1… final | 10 | 8 | 4 / 9.75 s / 48.7 % | 2 / none / 19.3 % | 2 / none / 12.6 % | 2 / 13.25 s / 45.4 % |

Every collection retains 8/8 complete cases with clean shutdown and no death.
The solo corner worlds are close to repeatable for the static and 60 u/s
cases and for the red 250 cases, and diverge for the matched 120 u/s red case
and the sustained blue case, where the runner sometimes leaves the proxy
behind within seconds and sometimes remains re-obstructed for 6..13 s. These
solo pursuit numbers are development evidence for the branch, not a Q3 stall
or escape classification; Q3's defended cohort remains the quality gate.

## Retained evidence

- `.codex-research/strafe-q1-shadow-20260909/` (four valid cases, four invalid attempts), `-b` (four reruns), `-final` … `-final5` (eight cases each on every intermediate build; `-final5` is the final revision), `strafe-q1-analysis-20260909/shadow-summary*.json`.
- `.codex-research/strafe-q1-falltrace-20260909/` (`plan.json` … `plan-5.json`, `sample-1` … `sample-5`, audits; `sample-1` holds the captured pit entry).
- `.codex-research/strafe-q1-regression-20260909/`, `-b`, `-c`, `-d` (retained failed attempts) and `-e` (final: 30 resource cases, 26 temporary-body cases).
- Mocked checks: `strafe-recovery-episode-check-20260909-q1a`, `localnav-reentry-q1-5d2c1b7e9a3f4c8d`, `strafe-route-resume-check-20260909-l` (earlier letters bound superseded gates).
- Launchers: `.codex-research/strafe-q1-shadow-launch.py`, `strafe-q1-shadow-rerun-launch.py`, `strafe-q1-shadow-final*-launch.py`, `strafe-q1-regression-launch*.py`.

## Status and next decision

Q1 is executed. Its deliverables are the pursuing-body fixture and audits,
native `corner_continue` execution evidence with displacement and objective
progress under pressure, the trace columns and pit-entry/contact audit, the
corrected route rejoin policy with its mocked check, and the retained
regressions (resources 30/30 and temporary-body 26/26 pass on the final revision `…e8f1…` (`.codex-research/strafe-q1-regression-20260909-e`), after four rejoin-gate variants that each failed the body suite were retained and replaced). The R8 `adapter-s4` death remains unclassified as to
avoidability, with the mid-air body contact as the documented candidate
mechanism; capture instrumentation is in place for any recurrence.

Two observations are carried forward rather than fixed here:

- Sustained pursuit by a body that always stands in front at running speed
  produces 6..13 s confinements in some runs. Q3's defended cohort, not this
  proxy, decides whether that is acceptable movement quality.
- With the route unavailable for 700 ms, ground fallback carried the runner
  about 800 units to a dead end where it stood still for 17 s
  (`strafe-q1-regression-20260909-c`, `blue_respawn_4`/`red_respawn_4`). The
  final rejoin policy avoids that trigger, but the fallback's own dead end is a
  stall exposure for Q3.

Next is Q2 (independent matched-resource references), then Q3, Q4 and Q5 in
the handoff order. Do not rerun the Q1 protocols without a new hypothesis; the
eight-case shadow launcher and the trace worlds are declared and repeatable if
Q3 needs them. K07 remains unqualified; nothing was installed.
