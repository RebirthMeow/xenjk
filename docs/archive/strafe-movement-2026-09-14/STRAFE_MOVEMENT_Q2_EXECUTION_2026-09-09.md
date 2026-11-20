# Q2 execution: independent matched-resource references (2026-09-09)

Q2 of the [post-R9 order](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE_HISTORY_2026-09-12.md#q2--implement-independent-matched-resource-references)
implements the reference design from the
[R7 record](STRAFE_MOVEMENT_R7_EXECUTION_2026-09-08.md#next-implementation-package-matched-resource-planned-references).
Starting checkpoint: `f9bdd662` (Q1 final). This document declares the loader
format, the finite proposal alternatives, the selection rule and the 30
candidate/reference pairs **before** any native world; the sections after
*Declared design* record what was then implemented and measured.

## Declared design (frozen before collection)

### Vocabulary

- **Candidate arm**: the production configuration under qualification,
  `--supported-corners 2 --flight-artifact <r3 payload> --flight-adapter --forecast-guard`,
  acquiring its ground path from the live `NavMesh_GetPath` query as today.
- **Reference arm**: the identical configuration and initial condition, with
  the acquisition query replaced by frozen, independently planned XYZ knots
  loaded at the same boundary. Everything after acquisition (preparation,
  support/sweep/headroom/corner validation, shared execution, flight adapter
  admission, objective dispatch, native resources, natural Speed expiry) is
  the unchanged production code. The reference differs from the candidate in
  one input only: where the raw polyline comes from.
- **Leg**: outbound (`MACRO_FETCH_FLAG`, own stand to enemy stand) is leg 0;
  return (`MACRO_RETURN_FLAG`, enemy stand to own stand) is leg 1.
- **Case**: one of the existing 30 resource conditions
  `team x phase x (force, expiry)` with teams red/blue, evaluation phases
  0/3/6, conditions (0,0) (20,0) (50,0) (20,400) (20,8000). Selection uses
  the same teams and conditions on phases 1/4/7.

### Payload format `BOT2REFPATH1`

Whitespace-separated tokens parsed with the shared `COM_ParseExt` tokenizer
and the same stream guard as `BOT2FLIGHT1`. Every field is mandatory; any
malformed, out-of-range, duplicate or trailing token refuses the whole file
and leaves nothing loaded.

```
BOT2REFPATH1 <wire version = 1>
<sv_mapChecksum unsigned> <bsp sha256, 64 lowercase hex>
<gravity> <basespeed> <pmove_fixed> <pmove_msec> <pmove_float> <game step ms>
<team 1|2> <phase 0..7> <initial force 0..100> <initial Speed remaining ms 0..60000>
<inputs sha256, 64 lowercase hex> <alternative id token>
<leg count 1..2>
  <leg 0|1> <knot count 2..200> <x y z> * knot count      (per leg, legs unique)
```

Binding at load (once per map load, at the first acquisition that wants it):
map checksum and the six physics values are compared exactly against the
live server, as the flight artifact does; team, phase, force and expiry are
compared against the client's team and the run's `bot_flagrun_phase`,
`bot_hopForce`, `bot_hopExpiry`. That is the **initial condition identity,
bound once**; the return leg is never rebound to the initial force or expiry
and consumes whatever resources the native game actually left. Knot spacing
must be 1..2048 units per edge. The `inputs sha256` is the digest of the
frozen `selection.json` that names every planner input by hash; the runtime
only carries it into telemetry so the auditor can bind the tape to the plan.
The loader never reads clearance, hop or speed flags: there are none in the
wire, by construction.

### Acquisition contract

At the existing `!path->count` branch in `ai_bot2_supported.c`, when
`bot_supportedReference` names a payload:

1. Ensure the payload is loaded (refusal prints `[REFERENCE] artifact_refused`
   once and every later acquisition prints `[REFERENCE] refused reason=not_loaded`).
2. Leg from the macro state; a leg absent from the payload refuses `no_leg`.
3. The dispatched target must lie within 64 units of the leg's final knot,
   otherwise `endpoint` (a dropped flag or a changed objective is not the
   planned leg).
4. Project the current origin onto the leg polyline; the nearest segment must
   be within 64 XY units and 72 Z units, otherwise `off_path`.
5. The raw polyline becomes the current origin followed by the knots after
   that segment. It then enters `Prepare` exactly as a navmesh answer would;
   the existing invalid-query gates still apply.

Every refusal falls back to the ordinary navmesh query in the same frame so
the run continues, and is printed `[REFERENCE] refused client= level= leg= reason=`.
A reference-owned path prints `[REFERENCE] acquire client= level= leg= knot= count=`
and, at every release, `[REFERENCE] release client= level= leg= reason=`
with the existing rejection reason. **Any refusal on a leg invalidates that
leg's reference measurement for that case, even if the fallback later
captures.** Prints are bounded by the existing one-query-per-frame budget and
the 1 s retry after rejection.

Nothing changes when the cvar is empty. The candidate arm never sets it.

### Finite proposal alternatives

Inputs (all hashed into `selection.json`):

- Mode-3 geometry: `hop_geometry.csv` written by the production observer's
  own flag-path survey in the bound Q1 resource runs
  `strafe-q1-regression-20260909-e/resources/red-f0-p0-e0` (red stand to blue
  stand) and `blue-f0-p0-e0` (blue stand to red stand). Red leg 0 and blue leg
  1 start from the red survey; blue leg 0 and red leg 1 from the blue survey.
  Each survey is an actual navmesh answer for that direction; no polyline is
  reversed.
- Support inventory: `strafe-map-preparation-20260906/kejim-v2/support.json`
  (hash-bound to the Kejim pk3/BSP/navmesh; collision-unvalidated).
- Stand endpoints from the same inventory: red flag (152, -1160), blue flag
  (2464, 5312).

Alternatives, computed by `tools/strafe_reference_path.py propose`:

| Id | Rule |
|---|---|
| `navmesh` | The survey's raw path points unchanged (frozen Detour answer). |
| `pull` | Greedy string pull over the raw points: from the current knot, keep the farthest later knot whose straight chord is *inventory-supported*, emit it, repeat. |
| `pull512` | The same pull with chord XY length capped at 512 units. |

A chord is inventory-supported when every 16-unit XY sample along it lies
inside (or within half a unit of the edge of) a ground polygon of the
inventory whose fan height at that XY is within 18 units of the survey's
linear height. Samples through off-mesh connections are not ground and fail.
This is proposal geometry only; native preparation decides support. Every
chord refusal is retained in `proposals.json`.

*Amendment before any collection:* the first declaration also required 24
XY units from every boundary segment. Dry runs showed that Detour's knots sit
exactly on boundary vertices and the survey polyline runs 9..23 units from
the boundary, so a 24-unit margin (`strafe-q2-proposals-20260909-a`) and an
8-unit interior-sample margin (`-b`) each refused every chord and made all
three alternatives identical. Containment only (`-c`, then `-d` after fixing a
duplicated start knot that the loader's 1-unit edge minimum would refuse)
removes the Detour bevel pairs: with knots on polygon edges counted as
contained (`-d`, half-unit edge tolerance), `pull` has 13 knots and `pull512`
15 on every leg against `navmesh` 18/17 (red/blue leg 0). The `-c` run
without the edge tolerance kept 15/14 and 16/15. All earlier proposal
directories are retained; `-d` is the declared proposal set.

### Selection rule

Run each alternative as the reference arm on **phases 1/4/7, both teams,
all five conditions** (30 worlds per alternative, 90 total), accelerated,
candidate configuration otherwise unchanged. For each `(team, leg)`:

1. An alternative **qualifies** only if all 15 of its runs for that team
   complete without a fixture refusal, the payload loaded in each, and the
   leg has **zero** `[REFERENCE] refused` events in all 15 runs.
2. Leg time per run: leg 0 is the first native pickup elapsed time; leg 1 is
   first native capture minus first native pickup. The alternative's score is
   the median over its 15 runs.
3. Select the qualifying alternative with the lowest median; ties choose the
   fewer knots.
4. If nothing qualifies, that `(team, leg)` has **no reference**; its 15
   evaluation pairs are reported as unmeasured, not passed.

The selected knots per `(team, leg)` are frozen by
`strafe_reference_path.py select` into 30 evaluation payloads (one per case,
identity bound) and `selection.json` (inputs, alternatives, refusals, scores,
selection run hashes). Selection uses reference tapes on phases 1/4/7 only;
no candidate tape and no phase 0/3/6 tape takes part.

### Evaluation pairs and audit

On one build, collect 30 candidate worlds and 30 reference worlds on phases
0/3/6 (60 worlds, at most four at once), then audit with
`strafe_supported_report.py --reference-runs`. Per case the auditor keeps all
existing gates on both arms and adds pair gates:

- same build binaries and source hashes; native settings equal except the
  reference cvar;
- actual initial pose (origin within 0.01), yaw (new `yaw=` field on the
  `[CONTINUOUS] START` line), zero velocity, force, active powers, phase
  (`level_time` residue) and physics equal on frame 0 of both tapes;
- the declared Speed deadline: equal `bot_hopExpiry` and, when nonzero, the
  single natural expiry within 25 ms of it in both arms;
- reference validity per leg: payload loaded, no refusal on that leg before
  the leg's first completion.

Reported per pair and per leg: candidate and reference times, idle and
walking fractions through first capture, reference acquisitions/releases
with reasons, flight entries and fallbacks in both arms, 120 s observation
retained. **No numeric candidate/reference ratio threshold is declared**;
`summary.json` carries `reference_ratio_policy: "undeclared"` and its
`passed` field covers gates, matching and validity only. Declaring a ratio
is a plan decision to surface, not to invent after seeing results.

### Native budget and ownership

Selection 90 worlds, evaluation 60 worlds, all accelerated outcome worlds at
four per batch; no timing claim. Mocked checks before any world:
`strafe_refpath_check.py` (loader), `test_strafe_reference_path.py`
(planner), `test_strafe_supported_report.py` (auditor). Files:
`codemp/game/bot2_refpath.{c,h}` (pure parse and lookup), `ai_bot2_supported.c`
(acquisition, prints, map-load reset), `bot2_continuous.c` (yaw and the
setting name), `ai_main.c` (one reset call), CMake, and the four tools above
plus the runner and batch flags. Fresh unique build after the C changes;
sources frozen during collection.

## Source changes

| File | Change |
|---|---|
| `codemp/game/bot2_refpath.{c,h}` (new) | Pure `BOT2REFPATH1` parser and projection lookup: shared COM tokenizer with the flight-lib stream guard, exact physics and identity binding, 1..2048-unit edge bound, at most 200 knots and two unique legs, atomic refusal. `Bot2_RefPathAcquire` projects the origin onto the leg polyline (64 XY / 72 Z envelope), checks the dispatched target against the final knot (64 units) and writes origin plus the suffix after the nearest segment. No trap/level/entity access. |
| `codemp/game/ai_bot2_supported.c` | `bot_supportedReference` loads once per map (`Bot2_SupportedLoadMap`, called from `BotAILoadMap` beside the flight adapter reset) at the first acquisition that wants it, binding map checksum, live physics, client team and `bot_flagrun_phase`/`bot_hopForce`/`bot_hopExpiry`. At the `!path->count` branch the reference is tried first and any refusal falls through to `NavMesh_GetPath` in the same frame. `path->reference`/`referenceLeg` mark ownership; `TraceRelease` prints `[REFERENCE] release` with the existing reason. Nothing else in preparation, corners, execution or handoff changed; with the cvar empty the file behaves as before. |
| `codemp/game/bot2_continuous.c` | `[CONTINUOUS] START` gains a trailing `yaw=` field (metadata and console); `bot_supportedReference` joins the recorded SETTING names. Every existing parser matches a prefix. |
| `codemp/game/ai_bot2.h`, `ai_main.c`, `CMakeLists.txt` | Declaration, the one reset call, build inputs. |
| `tools/strafe_refpath_check.py` (new) | Mocked Win32 check compiling the real parser and tokenizer: 43 refusal cases (every header field, identity, digests, knot count/range/spacing, duplicate leg, trailing/quoted tokens, live digest mismatch) and the projection contract. |
| `tools/strafe_reference_path.py` (new), `test_strafe_reference_path.py` | `propose` (survey reader, support inventory containment, greedy pull, identity-bound payloads for the 30 selection cases per alternative) and `select` (declared rule over selection batches, frozen evaluation payloads, `selection.json`). |
| `tools/strafe_continuous_runner.py` | `--supported-reference PATH` stages `reference_path.txt`, sets the cvar, binds the payload and `bot2_refpath.{c,h}` into the manifest hashes, records `report.reference` (loaded/refused/acquisitions/refusals/releases) and fails the run when the payload never loaded. |
| `tools/strafe_supported_batch.py` | `--supported-reference DIR` (one `<case>.txt` per case, all 30 required), `--phases`, `--workers`; `mode.json` records them. |
| `tools/strafe_supported_report.py`, its test | `--reference-runs DIR`: audits the reference arm with every existing gate and each candidate/reference pair (same build and settings except the cvar, frame-0 pose/yaw/velocity/resources/phase, Speed deadline, payload loaded), reports per-leg validity and both arms' times and travel fractions, and carries `reference_ratio_policy: undeclared`. |
| `codemp/game/ai_bot2_README.md` | Cvar row and file map. |

Checks on these sources: `strafe-refpath-check-20260909-b` (pass, 43 refusals;
`-a` is the same check with a miscounted expectation), `test_strafe_reference_path`
(8 tests) and `test_strafe_supported_report` (4 tests) pass.

Build: `.codex-research/strafe-build-q2-refpath-38e5638bd38c48ac9b04e210a79f5973`
(1104 source inputs, sources frozen during every collection below). Warnings
are the pre-existing ones in other files; none in the changed files.

## Proposals

`strafe-q2-proposals-20260909-d` (declared set; `-a`, `-b`, `-c` retained as
the amendment history above). Inputs: the two Q1 `-e` surveys (red 18 raw
points, blue 17), `kejim-v2/support.json` (539 ground polygons, 1071 boundary
segments, BSP `76537a48…`), map checksum 2993043243. Per leg the survey polyline
runs stand to stand with an 829/844-unit ramp at each end (−114/+118 units)
and 8-unit bevel pairs at five bends; `pull` collapses those pairs and two
30..34-unit shallow joins (13 knots per leg), `pull512` keeps the joins whose
merged chord would exceed 512 units (15 knots). 90 selection payloads written,
one per alternative and case, identity bound.

## Native smoke (`strafe-q2-smoke-20260909-a`)

One isolated accelerated world, red, phase 1, force 0, no Speed, candidate
configuration plus the `pull` payload. The payload loaded and bound (752
bytes, legs 2, knots 13/13, team 1 phase 1 force 0 expiry 0); the reference
was acquired at every leg start (knot 1, count 13) and released only by
objective change; zero refusals; pickups at 24.900/74.725 s, captures at
50.175/99.800 s, no deaths. The START line reports `yaw=83.597`.

## Selection (`strafe-q2-selection-20260909-a`)

Launcher `.codex-research/strafe-q2-selection-launch.py`; three batches of 30
accelerated worlds (phases 1/4/7, both teams, five conditions), four at a
time, about nine seconds of wall time per world; then
`strafe_reference_path.py select`. All 90 worlds completed with two captures,
no deaths, the payload loaded and bound, five reference acquisitions and zero
refusals each, so every alternative qualified on every `(team, leg)`. Median
leg times (ms) over the 15 selection runs per team:

| Team/leg | `navmesh` | `pull` | `pull512` | Selected |
|---|---|---|---|---|
| red/0 (18, 13, 15 knots) | 25375 | 24900 | **24625** | `pull512` |
| red/1 (17, 13, 15) | 26025 | **25275** | 25950 | `pull` |
| blue/0 (17, 13, 15) | 25850 | **25625** | 25875 | `pull` |
| blue/1 (18, 13, 15) | **24500** | 24550 | 24625 | `navmesh` |

The differences are 50..750 ms on 24..26 s legs; bevel removal is a small,
leg-dependent effect and the frozen navmesh polyline itself won one leg.
`selection/selection.json` (sha `aad5a312…`) binds the inputs, every score,
refusal count and the selection run directories; `selection/payloads/` holds
the 30 identity-bound evaluation payloads (alternative id `selected`, inputs
digest = the selection file). No candidate tape and no phase 0/3/6 tape was
read by the selection.

## Evaluation (`strafe-q2-evaluation-20260909-a`)

Launcher `.codex-research/strafe-q2-evaluation-launch.py`: 30 candidate worlds
(`candidate/`, ports 41600+) then 30 reference worlds (`reference/`, ports
41700+, frozen `selection/payloads`), phases 0/3/6, accelerated, four at a
time, about nine seconds each; then the pair audit. The first audit
(`pair-report`) failed every pair on `same_settings` because it compared the
isolated home path and port; `pair-report-b` (auditor ignoring exactly
`fs_homepath`, `net_port` and the arm cvar, with the unit test extended) is the
result and the first audit is retained as the auditor's own false failure.

Result: `passed: true`. Both arms pass all 20 existing gates in every case
(complete, two captures, alive, no stalls, cadence, clocks, phase, native
settings, loadout, no fire/power commands, no force spent, native scores, no
other powers, expiry, source freeze, build/module/engine binding, capture
hash). All 30 pairs pass the pair gates; 60/60 leg measurements are valid
(payload loaded in all, zero reference refusals). Reference ownership is
identical in every tape: three outbound and two return acquisitions, every
one at knot 1 from the stand, all 120 releases by `clock_or_objective` at
pickup or capture, never a rejection. One pair differs in flight entries
(`blue-f20-p6-e8000`: 13 candidate, 11 reference).

Reference minus candidate leg time, ms (negative = reference faster):

| Leg | Median | Range | Reference faster |
|---|---|---|---|
| 0 outbound (first pickup) | −462 | −900 .. +75 | 22/30 |
| 1 return (capture − pickup) | −300 | −700 .. +275 | 27/30 |

Idle fraction through first capture is no worse in the reference arm in
26/30 pairs and walking fraction in 21/30; both arms are 3.5..4.4 % idle
and 44..48 % walking. Candidate times repeat exactly across force conditions
at one phase (the grounded executor never spends Force), so the 30 pairs are
effectively six distinct trajectories per arm plus the e8000 Speed cases.

**No candidate/reference ratio threshold exists in the plan.** The auditor
reports `reference_ratio_policy: undeclared`; `passed` covers gates, matching
and validity only. Borrowing M1's ratio or inventing one after seeing these
numbers is not done here; whether a 2..4 % reference advantage should gate
K07 is a plan decision to take before Q5.

## Retained evidence

| Path | Content |
|---|---|
| `.codex-research/strafe-build-q2-refpath-38e5638bd38c48ac9b04e210a79f5973` | Q2 build, 1104 inputs |
| `strafe-refpath-check-20260909-a`, `-b` | Mocked loader check (`-a` miscounted expectation, `-b` pass) |
| `strafe-q2-proposals-20260909-a` .. `-d` | Proposal history; `-d` declared |
| `strafe-q2-smoke-20260909-a` | One native smoke world of the reference arm |
| `strafe-q2-selection-20260909-a` | 90 selection worlds, `selection/selection.json`, `selection/payloads` |
| `strafe-q2-evaluation-20260909-a` | 60 evaluation worlds, `pair-report` (false failure), `pair-report-b` (result) |

## Status and next decision

Q2 is executed. Deliverables: the loader and its mocked check, the planner
and selector with tests, the runner/batch/auditor extensions, the declared
and amended proposal rule, a zero-refusal selection over 90 worlds and 30
valid matched pairs on one build. Limits: the alternatives are bevel-level
edits of the navmesh polyline, not a different route family; every reference
ran the same flight adapter and corners as the candidate, so the comparison
isolates raw-path choice only; accelerated tapes carry no timing; the ratio
policy gap is open.

Next is Q3 (integrated quality on one candidate), then Q4 and Q5. For any
later acquisition or preparation change, rerun both evaluation batches with
the frozen payloads on the new build and audit with `--reference-runs`; do
not reselect unless the surveys or inventory change. K07 remains unqualified;
nothing was installed.
