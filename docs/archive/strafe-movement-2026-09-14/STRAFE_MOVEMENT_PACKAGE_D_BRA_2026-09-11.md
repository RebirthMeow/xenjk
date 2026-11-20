# BR-A: one complete branch transition, implemented and flown (2026-09-11)

Step 2 of the owner-approved
[branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md):
one complete BR-A with the minimal B3 loader/owner machinery and the B6
depleted-pickup choice, authored, executed through the production owner, and
validated natively in both directions.

**Nothing is installed.** The installed build remains
`strafe-build-reviewready-20260910-b` (`525f1b50`). This record establishes that
the transition works and what it cost to get there; it does **not** establish
that the branch makes returns faster, which needs the repeated candidate
campaign of step 4 scored against the
[control bands](STRAFE_MOVEMENT_PACKAGE_D_CONTROL_2026-09-11.md). K07 remains
open.

## What was built

| Part | Where |
|---|---|
| Intermediate terminal semantics | `bot2_strafe.h`/`bot2_strafe.c`: one `intermediate` flag gating the four rules that exist only for the final approach to a flag |
| Catalog hash | `ai_bot2_route.c`: FNV-1a over the asset's own bytes, masked to 31 bits |
| Branch sidecar | `bot_strafeBranches`, format `STRAFEBRANCH1 <catalog hash> <count>` then `lane entry branch exit rejoin role` per line |
| Sidecar validation | Header, hash, count bound, index ranges, matching teams, a rejoin ahead of its entry, no route declared twice, no branch that is itself a lane. Any failure refuses the payload **and the catalog with it** |
| Branch-role selection | A route named as a branch is never selectable as a lane, in all three selection paths |
| Branch execution | `fastReturn_t` carries `onBranch/branchRoute/branchDecl/rejoinIndex`; one accessor decides which guide the steering state belongs to; progress and refusals carry their route |
| B6 depleted choice | A refused launch takes a declared branch instead of handing the lane back — from the join scan and from an active lane, without costing a command frame |
| Rejoin | The published envelope below, plus the onward lane launch judged by the same ladder every other launch passes |
| Recovery | A finished branch whose rejoin is refused abandons once, by name, with a 2 s backoff |
| Authoring | `tools/strafe_branch_author.py` (BR-A from measured play), `tools/strafe_branch_sidecar.py` (bundle, hash, validate) |

## The published transition envelope

Every bound is numeric, declared in the source, and checked against the actual
state. XY proximity identifies a candidate; these authorize it.

| Bound | Value | Why this value |
|---|---|---|
| `BOT2_BRANCH_ENTRY_ARC` | 640 units along the branch | A depleted runner arriving at the top of this descent is airborne for its first ~340 units and can join nothing until it lands. An entry window expressed in *samples* means something different at every authoring resolution; this one does not |
| `BOT2_BRANCH_EXIT_XY` | 48 units | Measured ground convergence with lane sample 44 is about 13 units |
| `BOT2_BRANCH_EXIT_Z` | 32 units | Against the lane sample's prepared floor, both for the sample and for the runner |
| `BOT2_BRANCH_EXIT_HEADING` | cos ≥ 0.5 (60°) | Applied only above 80 u/s: a slow arrival has no meaningful heading |
| `BOT2_BRANCH_EXIT_VZ` | 64 u/s | Not mid-arc, even where contact is reported |
| contact | grounded | The rejoin is a grounded run point |
| jump latch | `PMF_JUMP_HELD` clear | Never switch guides inside a latched jump |
| corridor | body and four floor traces | The same geometry check a lane join makes |
| onward launch | `FastReturnContinuation` at the rejoin | A branch can never deliver a runner into a flight it cannot afford |

There is deliberately **no speed floor** in the exit gate. The momentum ladder
already refuses a launch the runner's speed cannot support, and a floor here
would strand a slow runner on a branch it had already completed.

Separately, a branch is refused outright unless **every one of its samples
stands on prepared floor** (`FastReturnBranchUsable`). A branch is a ground line
by construction — the runner that needs one cannot afford a flight — and the
reason that gate exists is in the next section.

## What the native runs cost, and what they found

Four defects, each found by a native run and fixed where it happened. They are
recorded because the first three would each have been easy to call a tuning
problem, and none of them was.

1. **A `select` record per frame.** 553 identical "no admissible entry" lines in
   one world. Standing outside a branch's entry is a state, not an event; it is
   now recorded at 250 ms.
2. **The abandon accounting never fired.** A runner that flies past the end of
   its branch leaves the envelope *before* reaching the last sample, so the
   release was filed as an ordinary `envelope` failure and no branch was ever
   recorded as abandoned. Past the exit, leaving the envelope is now the
   abandoned rejoin, by name.
3. **The authored line fell 1776 units into a pit, in both directions.** The
   first station table took the median of *every* measured sample in each y
   band. Between y = 2700 and y = 2200 that median is the **hop arc** those
   runners fly, which passes to the right of the ground route and over a gap.
   Every sample of that line was within 15 units of a position play had reached
   — while airborne. The corridor record's own convergence figures at lane
   samples 42/44 (0.9–2.2 units) are airborne convergence for the same reason:
   **on the ground the corridor passes about 89 units from lane sample 43 and
   about 13 units from sample 44.**
   Three things changed: the stations are now medians of *grounded* samples
   only; the author reports grounded and airborne evidence separately and
   refuses a line whose samples have no grounded measurement within 160 units;
   and the runtime refuses any branch with an unsupported sample, so an
   authoring mistake of this shape can no longer be executed at all.
4. **A tap into a turn flew straight over the edge**, a 500-unit fall, again in
   both directions. A tap is ballistic: the runner keeps the heading it left
   with, and the measured ground route turns ~70° between y = 2700 and y = 2600
   to get around that same gap on foot. Taps now require the guide to stay
   within 12° across the tap's own flight.

A fifth correction was a contract, not a defect: the declared rejoin moved from
lane sample **43 to 44**. Sample 43 is the launch that crosses the gap; a runner
arriving on the ground has already crossed it on foot, so 43 is a launch it no
longer needs rather than one it skips.

## BR-A as authored

29 → 44 samples after the corrections, from stations measured in the three
control repetitions: x and z are medians of the **grounded** samples in each
50-unit y band, and a band with fewer than four grounded samples contributes no
station at all. The corridor floor is one flat plane at z = 1808.1 from y = 4400
to the rejoin, which is what makes an affordable tap chain possible.

- **Entry** at (2472, 4900, 1914.5), the lane's own run-up height.
- **Descent** through the y = 4500 choke to the corridor floor.
- **Corridor** as a tap line: `up = 1`, never 127, six taps, each on a flat
  stretch straight to within 12°.
- **Rejoin** at the lane's sample 44 position, on the floor.
- **Blue** is the exact half-turn about (1312, 2080).

The native preparation confirms the authoring: `prepared route=26 samples=44
usable=1 first_unsupported=-1` in both directions, and every authored launch is
classified `class=0 tap=1` — a checked tap chain, affordable at zero Force. The
lane's own launches, by contrast, are all class 10 or 20 and several are marked
`gap=1`; that is why a depleted runner can afford none of them and needs this
branch at all.

Catalog capacity: 28 of 32 routes used, four slots left.

## Native evidence

Build `strafe-build-bra4-31a4ef26…`, 1104 source hashes matching the checkout,
worlds retained at `.codex-research/pkgD-bra-native5-20260911`. Four seeded
cases, both directions, 55 game seconds each, the same world configuration as
the control arms.

| Case | Entry | Travel | Transition |
|---|---|---|---|
| R2-red (12 Force) | join scan, index 7, 243 u/s | 7 → 43 of 44 | abandon `takeoff_receiver_reserve` — the lane's next flight needs 20 Force |
| R2-red (later) | lane refusal `takeoff_momentum`, 492 u/s | 0 → 44 | **exit admitted at lane 44** |
| R4-red (0 Force) | join scan, index 9, 250 u/s | 9 → 43 | abandoned |
| R4-red (later) | lane refusal, 502 u/s | 0 → 44 | **exit admitted** |
| R2-blue (12 Force) | join scan, index 7, 266 u/s | 7 → 43 | abandon `takeoff_receiver_reserve` |
| R4-blue (0 Force) | join scan, index 8, 235 u/s | 8 → 43 | abandoned |
| R4-blue (later) | lane refusal, 492 u/s | 0 → 44 | **exit admitted** |

All four worlds: **2 captures, 0 deaths, minimum health 90** — no falling damage
at all. The same cases on the broken authored line died once per world.

After an admitted rejoin the runner is on the lane and stays there: from 392 u/s
at the transition it accelerates through the lane's hop chain to 948 u/s over
the next 2.5 s with no release. That is the onward execution the plan requires,
not a capture through the fallback.

The abandons are as informative as the exits. A 12-Force runner completes the
whole branch and is then refused the lane's next flight by name
(`takeoff_receiver_reserve`, which wants 20 Force). The branch delivered it; the
lane could not take it. That is the honest state of the post-branch obligation.

### The choke the branch was built for

Matched on the same four seeded cases, carried samples on the corridor side of
the map, 4 Hz:

| Window | Control | Branch |
|---|---|---|
| **y = 4500 (the choke)** | **median 139.9** (n=24) | **median 403.7** (n=12) |
| descent, y 4400–4750 | median 381.6, 47.3 % below 300 | median 405.8, 12.9 % below 300 |
| corridor, y 2200–4400 | median 427.3, 28.1 % below 300 | median 425.1, 18.0 % below 300 |
| descent ownership | supported 53, lane 2 | lane 21, supported 10 |

Measured **while the branch owns the runner**, against the plan's declared
T2-branch minimum of 200 u/s through descent and rejoin:

| Window | n | median | min | below 200 |
|---|---|---|---|---|
| entry | 7 | 465.0 | 428.4 | 0 |
| descent | 21 | 405.4 | 250.1 | 0 |
| corridor | 146 | 425.2 | 274.7 | 0 |
| rejoin | 11 | 425.3 | 392.5 | 0 |

Every sub-200 sample in these windows belongs to `supported` ownership — before
an entry or after an abandon — never to the branch.

**What this is not.** Four worlds, one repetition, 7–146 samples per window at
4 Hz. The control bands say joins, releases and churn vary 26–36 % across
identical repetitions, and the corridor median is unchanged (427 → 425) exactly
as the corridor record predicted. The choke figure is a first native window
measurement on the station the corridor record named, not a campaign result and
not a claim about return time.

## Checks

| Check | Result |
|---|---|
| `strafe_catalog_check` | PASS — adds sidecar binding, **16 sidecar refusals**, the catalog fail-closed rule, and branch selection exclusion |
| `strafe_fast_return_check` | PASS — adds branch entry from both paths, the entry window, floor usability, the published rejoin envelope bound by bound, abandon recovery with backoff, the admitted rejoin, and route-keyed refusals |
| `strafe_intermediate_tail_check` (new) | PASS — the four final-approach rules do not fire on an intermediate route, and **all four still fire exactly as before when it is clear** |
| `strafe_route_resume_check`, `strafe_supported_wait_check`, `strafe_corner_track_check`, `strafe_hop_arrival_check`, `strafe_flightplan_check` | PASS |
| Python suites | 409 tests, all passing |
| 26 temporary-body cases (frozen M2, evaluation, accelerated) | **26 / 26 pass, 0 failures** |
| 30 resource cases (accelerated, supported corners 2, staged flight adapter) | **30 / 30, `passed: true`** |

Both regression suites match their last recorded status, so the branch work
costs nothing on either. Evidence under `.codex-research/pkgD-checks-20260911`,
`pkgD-fr-check-final`, `pkgD-tail-check-20260911` and `pkgD-regression-20260911`.

## What is still open

*Updated 2026-09-11: the first two items below were closed by step 3; see the
[BR-B record](STRAFE_MOVEMENT_PACKAGE_D_BRB_2026-09-11.md). They are kept here
with their outcomes because this record's measurements were taken before that
work and still describe the build they were taken on.*

- ~~**The post-group-1 row is not closed.**~~ **Closed.** A runner that flies
  group 1 and then cannot afford group 2 now takes BR-B from its landing-platform
  state. BR-A's entry is on the run-up, not on that platform, and BR-B's is.
- ~~**A 12-Force runner completes the branch and is then refused the lane.**~~
  **Closed, and it was not a resource problem.** The rejoin was admitting against
  the launch at lane 43 — the one the branch exists to walk around — because the
  kernel's takeoff window reaches three samples behind the rejoin at 44. The gate
  now asks about the next *executable* maneuver, and the same two cases in both
  directions rejoin the lane at 425 u/s.
- **Takeoffs 50 and 57 remain unbranched**, as does every other momentum-refusal
  family.
- **No speed or churn claim.** The repeated candidate campaign against the
  control bands has not run; this record deliberately makes no aggregate claim
  from four worlds.
- The blue-only choke at mirrored y = 4300 is still unexplained, and its trace
  still has not been read.
- Nothing is installed, and K07 is not qualified.
