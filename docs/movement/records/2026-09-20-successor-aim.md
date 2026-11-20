# Second-hop proposals from the simulated landing

The actual-landing proposal improves the matched eight-pair mean from
**18,840.625 to 17,943.75 ms**, median from **18,512.5 to 18,125 ms**, and
maximum from **20,350 to 19,375 ms**. Six pairs improve and two regress.
The **16,450-ms** outcome appears three times in confirmation, in addition
to one pilot. Roughly 15 seconds is still unachieved; the fast outcome is
not the median. Recorded search cost increases substantially.

This comparison uses the same new build in both arms, with the earlier winning
`bot_moveReroute=1` and `bot_moveExitSpeed=2000` configuration. Only
`bot_moveSuccessorAim=0/1` and the isolated port differ. The earlier campaign's
17,900-ms median belongs to a different cohort, not this matched control.

## Implementation

`LandingAimOffers` appends at most one proposal for each original geometric
two-hop candidate. A guarded native forecast runs until the first landing
has been observed. The recorded **beforePS landing position** supplies the
origin for aiming at the original second-hop target. It preserves the first
hop, both run-ups, plan origin and all other plan inputs. The full existing
judge then rerolls the complete sequence from the unchanged original live
root, carrying native velocity, timing, jump, animation and resource state.
No graph nominal exit or synthetic player state is substituted into execution.

The preliminary measurement is only a construction hint. It cannot admit a
candidate. Missing/off-graph first landings produce no clone; guard or world
restoration failures abort the search. Appended slots are reset, capacity is
bounded, and clones cannot recursively generate clones. Existing sweep variants
and observe-only stored-heading candidates are excluded. Every native fall,
trigger, path, onward, route and walking-reference check remains in force.

`bot_moveSuccessorAim` is default off. This experiment is part of the existing
fixture-gated search, not ordinary multiplayer enablement. `aim_source` records
the original candidate index; commits report `aim_offers` and the selected
source. Replay analyzer version 5 excludes these clones from historical A/B
and 0-versus-24 run-up pairing tables while preserving all raw rows and overall
candidate/admission/commit counts.

## Confirmation: eight fresh counterbalanced pairs

Odd pairs run off then on; even pairs on then off. One native timing world is
active at a time. All 16 complete with zero deaths and movement ownership only.

| Pair | Off ms | On ms | Difference ms |
| --- | ---: | ---: | ---: |
| 1 | 17,900 | 19,200 | +1,300 |
| 2 | 20,350 | 17,200 | -3,150 |
| 3 | 18,800 | 16,450 | -2,350 |
| 4 | 17,900 | 19,375 | +1,475 |
| 5 | 20,275 | 19,375 | -900 |
| 6 | 18,225 | 16,450 | -1,775 |
| 7 | 17,900 | 16,450 | -1,450 |
| 8 | 19,375 | 19,050 | -325 |

Mean improvement is 896.875 ms (4.76%); median improvement is 387.5 ms (2.09%).
The enabled arm offers 513 successor proposals and selects 33 inside safe
outbound windows. There are six distinct common-prefix logged control traces
and five enabled traces; the three 16,450-ms runs share a trace. Fresh processes
and duplicate outcomes are not independent random samples. These are repeated
descriptive results, not a statistical confidence claim.

Pilot, excluded from confirmation: off 18,800 / 20,275 / 17,900 ms; on
19,050 / 19,625 / 16,450. All six pass.

## A matched local connection

The fast enabled trace matches four retained 17,900-ms controls in every
logged momentum field through relative 1,675 ms. At the same initial candidate
root, relative 450 ms, a selected clone changes second-hop relative heading
from -1.39499 to -1.52032 degrees while preserving first heading and run-up.
At relative 1,375 ms, both actual first landings are
(197.5, -779.4, 1912.1) at 364.7 units/sec. At the second landing, relative
2,425 ms, control reaches (259.5, -335.4, 1816.4) at 506.8 units/sec; enabled
reaches (261.9, -335.0, 1816.3) at 514.7. The first logged difference is a
command at relative 1,700 ms. Later several proposals and routes differ, so
the full 1,450-ms saving cannot be assigned to this first connection alone.

## Two additional experiments completed

**An intermediate 8-unit run-up regressed.** It was appended after 0 and 24
to preserve their tie order, with candidate capacity derived from the new
count. A fresh build compared against the frozen successor build, both with
all three experimental settings enabled. Baseline times were
16,450 / 19,050 / 16,450; the new vocabulary took 19,650 in all three runs.
Each selected five 8-unit run-ups. All pass, but mean is 2,333.33 ms slower and
cost rises. The vocabulary change was reverted; its complete source snapshot,
build and evidence are retained. Current native sources match the successor
build byte for byte. The historical 0-versus-24 report does not describe the
8-unit vocabulary; the dedicated summary counts every committed run-up.

**Separating rerouting and exit credit did not establish a replacement.**
With successor aiming on, three serial blocks rotate four arms:

| Reroute / exit credit | Times ms | Mean ms | Median ms | Maximum ms |
| --- | --- | ---: | ---: | ---: |
| 0 / 0 | 18725, 20000, 18725 | 19,150 | 18,725 | 20,000 |
| 1 / 0 | 17500, 17500, 20100 | 18,366.67 | 17,500 | 20,100 |
| 0 / 2000 | 20825, 19700, 19700 | 20,075 | 19,700 | 20,825 |
| 1 / 2000 | 19050, 19050, 16450 | 18,183.33 | 19,050 | 19,050 |

All 12 pass, each arm has two distinct common-safe traces. Rerouting alone
has a better median but worse mean and maximum than both, with essentially
equal recorded search cost. This small descriptive screen does not justify
replacing the combination or claiming an isolated causal contribution.

## Verification, cost and limits

The actual extracted constructor and first-landing policy pass tests for the
measured origin, unchanged first-hop inputs, exclusions, slot reset, capacity,
guard failures and termination. A wrong-origin negative control fails. The
replay pairing regression fails before the tool repair; all 51 replay tests
pass afterward. No native sources were edited during build or collection.

Final outcome qualification is **74/74 passing**, zero deaths, with movement
ownership only: default floor 36/36 (aim/reroute/credit disabled), enabled
floor 36/36 (aim1/reroute1/credit2000), and seeded `blue_out_L1_stand` plus
`red_ret_L1_stand` 2/2. Floor worlds run at most four in parallel and their
timings are not used for speed comparisons. All 1,117 native source hashes,
74 staged binary pairs and declared fixture settings/resources are verified.
The floors and named cases have 5,588 / 5,596 / 257 parity refusals, respectively,
with zero comparisons and mismatches. Level 2/3 floors retain their declared
Force settings; only the level-1 comparison is no-spend.

Confirmation staged binaries, build/runner/library/seed/case bindings and
settings are independently checked. No sampled active powers or Force-pool
decreases occur inside the conservative outbound windows. These observations
do not assert unsampled per-frame coverage. Stationary Force-0 case, unchanged
spend policy, native regeneration and fixed 1 / msec 8 / float 1 / fps 40
physics remain bound; timescale is 5. No human corridor or tape is used.

Recorded per-search rollout cost rises from mean **23.37 to 39.18 ms**,
median **22.2 to 38.9**, p90 **36.9 to 67.1**, and maximum **58.5 to 99.2 ms**.
These are fixture measurements, not controlled production CPU qualification.
Confirmation parity has **zero comparisons**, zero mismatches and 1,253/1,086
refusals for off/on. The new proposal is not a parity qualification.

## Bindings and next work

Main evidence: `.codex-research/successor-aim-20260920-a/`, including protocol,
`summary-confirm-final.json`, `confirmation-binding-check-1.json`,
`fast-trace-match-1.json`, `summary-ablation-final.json` and
`ablation-binding-check-1.json`. The local `summarize.py` and `fast_trace.py`
reproduce the reports into new files and bind their inputs; use `--phase confirm`
or `--phase ablation` for the completed comparisons. The unused planned
`simplify-confirm` phase was not collected because the screen did not establish
a better replacement. `qualification-summary-2.json` verifies all 74 outcomes;
its predecessor is retained with a corrected validator false positive about
the deliberately different seeded and unseeded graph bindings.

Build: `.codex-research/successor-aim-build-20260920-a`, manifest
`20260920T082745909836Z.sources.json`, with 1,117 native inputs. Focused C test:
`.codex-research/successor-aim-test-20260920T082956Z-e4f651a2`.
The rejected run-up evidence is `.codex-research/short-runup-20260920-a/`,
including `summary-pilot-1.json`, `experimental-source` and `baseline-source`;
its build is `short-runup-build-20260920-a`. No evidence was deleted.

Next: explain the slower successor outcomes and improve the useful continuation
set from actual native successor state, rather than trusting a single fastest
run. The second target is still geometrically proposed from the original
path; the new construction corrects its origin, not the full trip objective.
Avoid multiplying expensive forecasts before addressing repeated first-flight
work and establishing a runtime budget. Keep the 15-second objective, positive
parity, interruption recovery and production integration open.
