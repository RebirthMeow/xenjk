**2026-09-07 R3 note:** the offline-ranked payload was re-baked from runtime
oracle evidence (`--runtime-oracle`, see the
[native log](STRAFE_MOVEMENT_FLIGHT_NATIVE_2026-09-07.md), ninth section):
the geometry is unchanged, 13 windows' candidate lists are re-ranked by
completion at actual preparation roots, and `artifact.json` records the
matrix and bindings under `runtime_rerank`. The rest of this document
describes the representation, which stands.

# K05 prepared-movement artifact: representation and evidence

Current execution direction is in the
[runtime review](STRAFE_MOVEMENT_RUNTIME_REVIEW_2026-09-07.md). This document
retains design history and implementation constraints; its earlier unrun/next
statements are historical. The artifact/adapter now run natively, while complete
runtime cost and time/idle quality remain unqualified.

Started 2026-09-07. This document owns the K05 artifact design and the measurement
that chose it. The [active plan](STRAFE_MOVEMENT_PROJECT_PLAN.md) owns the gates,
the [scorecard](STRAFE_MOVEMENT_PROGRESS.md) owns status, and the
[implementation guide](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) owns K05's contract.

Nothing here is a runtime measurement. The baker, the native loader and the
opt-in production adapter all exist and the game DLL builds with them, but no
bot has executed a baked window in a live match, no cost has been measured, and
nothing is installed.

## What K05 has to replace

At each live entry the CHAIN2 discovery oracle runs two native ground prefixes of
64 steps each **and** seventeen 256-step candidate rollouts, then picks the shortest
useful one. The guide forbids requiring both in production. The 17 rollouts are
4,352 forecast steps; the prefixes are 128. The rollouts are the cost.

## The representation that measurement refused

The obvious artifact bakes, per window, the discovered takeoff boundary and the
selected candidate, so the runtime does neither the prefixes nor the rollouts.

Both halves fail against the existing evidence.

**Takeoff boundaries are not stable enough to bake.** Comparing the four recorded
boundaries per stage across four independent native runs of the same schedule
(K02 development phase 0 plus the K03 gate at phases 0/3/6), positions agree well
(max XY drift typically 1–8 units, inside the kernel's 12-unit tolerance) but
velocities do not. Roughly a third of (stage, candidate) pairs drift past the
kernel's own 40 unit/s velocity tolerance, with observed maxima of 80, 100 and
141 unit/s. One stage of `red-window-primary` also drifts 140–147 units in XY.
A baked boundary would therefore be refused outright at a large fraction of live
entries.

**The selected candidate is not stable either.** Discovery re-selects per entry
because entry states differ. `red-window-primary` stage 3 selected candidate 2,
2, **16** and 2 across four runs — sometimes the answer is "do not fly". Over the
whole cohort, grouping every observed stage by the window it executed and keeping
only windows with at least four independent runs, the distinct-selection count is:

| distinct candidates selected | windows |
|---|---|
| 1 | 3 |
| 2 | 5 |
| 3 | 3 |
| 4 | 1 |

Only 3 of 12 well-observed windows are single-candidate. The maneuver library's
proven "at most two policy IDs" (`int result[2]`) does not carry over.

This is the same failure mode the empirical-region coverage representation hit in
September (4 of 32 unseen roots refused, envelope deliberately not padded). The
plan's rule — review the representation rather than sweep again — applies.

## What the evidence does support

Two facts rescue the design.

**Every well-observed window has at least one candidate that was useful in every
observed run.** So a ranked list always has a reliable anchor and a window is never
baked as something that can refuse outright.

**For most windows a fixed candidate costs almost nothing.** Taking the best
always-useful candidate per window and comparing its rollout length against the one
discovery actually chose:

| window | runs | worst penalty (frames) |
|---|---|---|
| red:f01:v1, red:f06:v1, blue:f02:v0, blue:return:f02:v1 | 8–12 | 0 |
| red:f02:v1, red:return:f06:v0, blue:f06:v0, blue:return:f01:v1 | 9–14 | +1 |
| blue:return:f06:v0 | 9 | +5 |
| blue:f01:v0 | 12 | +6 |
| red:return:f01:v0 | 15 | +16 |
| red:return:f02:v0 | 13 | +84 |

One frame is one native command, so 25 ms. Summed over the selected schedules'
six stages, worst case:

- `blue-window-primary`: +13 frames = 325 ms. The K03 gate falls 12.71% → **12.14%**.
- `red-window-primary`: +102 frames = 2550 ms. The gate falls 10.74% → **6.29%**.

So a single fixed candidate holds blue and **breaks red**, and the damage is
concentrated in two return windows, one of which contributes +84 frames alone.

## The chosen representation

Bake window **geometry** plus a **ranked candidate list sized by measured need**.
Leave takeoff boundaries to the live native ground preparation, which is the cheap
half. The runtime tries the ranked candidates in order and takes the first that
validates, so an unreliable-but-fast candidate can lead and a reliable one anchors.

Ranking reproduces discovery's own rule exactly — a candidate is usable only if it
is `useful` (completed **and** every local check passed, not merely a passing final
receiver), ordered by mean rollout length, then supported before point before
flight, then lowest id. The list is truncated at the first candidate measured
useful in *every* observed run.

Baked from the K02 development cohort plus the K03 gate:

| candidate list size | windows |
|---|---|
| 1 | 28 |
| 4 | 3 |

The three needing four are exactly the unstable windows above: `blue:f01:v0`,
`red:return:f01:v0`, `red:return:f02:v0`. Per round trip that is 12 rollouts for
red and 9 for blue, against 102 for full discovery — an **88% and 91% reduction**,
with the 128-step ground prefixes retained. That satisfies the guide's constraint
by dropping the sweep, not the prefixes.

## Artifact format

`BOT2FLIGHT1`, an ASCII token stream re-parsed on load, following
`BOT2LIBRARY1` and `BOT2WINDOWSEED1`. The struct is never serialized, so the
artifact carries no endianness, padding or ABI dependency.

Header: magic, wire version, map checksum, BSP sha256, gravity, basespeed,
`pmove_fixed`, `pmove_msec`, `pmove_float`, game step ms, the four motion speeds,
root admission radius and minimum speed, window count, unsupported-region count.

Per window: team, leg, point-applicable flag, candidate count, the ranked ids, the
four geometry points, then three polygon blocks. Per unsupported region: team, leg,
refusal reason, approach point.

Guide-only seeds carry **no** support polygons — they emit three zero counts, and
the baker refuses to bake a point candidate (ids 8–15) or accept polygons for such
a window. No support geometry is ever invented.

Unlike the maneuver library, which defers every binding check to selection time,
the loader must validate map checksum and physics **at load**, following
`LoadTransition`'s pattern. A stale, corrupt or incompatible artifact refuses whole
and the existing fallback chain runs unchanged.

## Current state

The offline baker and the native loader both exist. `tools/strafe_flight_artifact.py`
and `tools/test_strafe_flight_artifact.py` (11 tests) bake from the qualified
K02 + K03 collections and the 88-seed report: **31 windows, 49 retained unsupported regions, 9,207 bytes**
(the library precedent is 445 bytes, coverage 2,002). The 49 regions are the
cohort's 36 receiver-bind refusals, 5 missing roots and 8 geometry refusals;
geometry-refused seeds have no declaration and so are retained in the manifest
without coordinates rather than given invented ones.

The baker refuses an unqualified campaign, a changed geometry binding, a cohort
spanning more than one map or BSP, and a window with no candidate usable at every
observed entry.

The current artifact is
`.codex-research/strafe-flight-artifact-4a724cbaf7da4b9b92f790a672313738/`.
Two earlier directories from the same session are retained and **superseded**:
`...-2287aaea307247c5b84e15f83de35f0e` and `...-8f90b2318d494a94a37cfd96dab7bd50`.
Both were written before the writer opened its files with `newline=''`, so Windows
translated the payload's single `\n` to `\r\n` and their `flight.txt` is 9,208
bytes rather than 9,207. Their token content is identical; only the byte stream and
therefore the sha256 differ. That mattered enough to fix because this project binds
every artifact by sha256, so a payload hash must not depend on the writing
platform. A re-bake of the current directory now reproduces its stored payload hash
exactly; that determinism check is worth repeating whenever the baker changes.

## The native loader

`codemp/game/bot2_flightlib.c/.h` parses `BOT2FLIGHT1` and looks a window up; it
is a CMake input of the game DLL. It is pure — no `trap->`, `level`, entity or
world access, checked mechanically — and holds up to 128 windows and 128 regions
against the 31 and 41 currently baked.

Four decisions are worth recording.

**Binding happens at load, not at selection.** The maneuver library defers every
check to selection time; this loader compares map checksum, gravity, basespeed,
`pmove_fixed`, `pmove_msec`, `pmove_float` and game step against a live binding
the impure caller supplies, following `LoadTransition`. Any mismatch refuses the
whole artifact. The BSP digest is the only optional comparison: an empty live
digest skips it, but the artifact must still carry a well-formed 64-character
lowercase hex one.

**Refusal zeroes the destination.** A failed reload therefore cannot leave a
stale library readable, and every lookup then reports no prepared movement, so
the existing fallback chain runs unchanged.

**The checksum helper is sign-safe by construction.** `Bot2_FlightLibChecksum`
normalises the engine's signed or unsigned spelling of the same 32-bit value the
way `LoadTransition` does. The K01 auditor defect came from comparing two
representations of one value; the new lineage cannot repeat it, and the check
loads the real artifact through the signed spelling specifically.

**Geometry rules are reused, not copied.** The loader must decide whether a
window's polygons are geometry a point candidate may run on. Duplicating the
convexity, degeneracy, area and containment tests would create exactly the
silent divergence that would let the loader admit geometry the transition kernel
would refuse. Those rules were therefore extracted from `Bot2_TransitionParse`
into an exported `Bot2_TransitionValidate(geometry, requireSupport)`, with
`PolygonValid` split out of `Polygon`; the parser now ends by calling it, and
the loader calls the same function. `requireSupport` false checks the two guide
directions alone, for a guide-only window whose polygons are legitimately empty.
The much smaller `TokenStream` guard from `bot2_windowseed.c` is duplicated
rather than shared: it is a stateless stream guard, not geometry semantics.

Root admission reproduces native discovery exactly
(`bot2_connectionbench.c:855-861`): same team and leg, 3D distance to the window
approach within `rootRadius`, world support, and velocity along the
approach-to-launch guide at least `rootMinimumSpeed`. Force state, `pm_type`,
health, objective publication and every native validation stay with the caller;
the lookup is geometry and contact only.

### What the loader check does and does not establish

`tools/strafe_flightlib_check.py` compiles the real loader, the unchanged
transition validator and the real shared COM tokenizer in Win32 and passes: 72
rotated artifact round trips, 211 atomic refusals, 103 bounded lookups, and the
actual 9,207-byte payload loaded to 31 windows and 41 regions through the signed
engine checksum. Refusals cover wire syntax and ranges, a guide-only window
offered a point candidate or carrying polygons, an applicable window missing
them, duplicated ranked candidates, duplicated polygon vertices, an anchor
outside its own support, every truncation of the stream, trailing content
including an empty quoted token, an unterminated quote, an over-long token, and
each physics field mismatching alone.

`tools/strafe_transition_refactor_check.py` compiles the pre-extraction parser
beside the current one and requires identical verdicts and identical parsed
geometry: **26,794 payloads agree, 9,837 accepted and 16,957 refused.** The
pre-extraction source is retained at
`.codex-research/strafe-transition-prerefactor-20260907/`, and its sha256
`48e998ba…` equals the entry the K05 source snapshot recorded for
`bot2_transition.c`, so the comparison is against the frozen revision rather
than a reconstruction that merely looks right.

None of this is a runtime measurement. Parsing an artifact establishes no native
support, clearance, publication, resource state, cost or usefulness. No adapter
exists, no bot has executed a baked window, and nothing is installed.

## The production adapter

`ai_bot2_flight.c/.h` is off unless `bot_flightArtifact` names a payload **and**
`bot_flightAdapter` is nonzero. With either unset the ordinary supported path
runs untouched.

### Where it had to go, and why not where the plan said

The constraint list below said to insert at Slot A, between priority-supported
(`ai_bot2_movement.c:418`) and the route (`:420`). **That placement cannot
work.** `Bot2_SupportedPriority` (`ai_bot2_supported.c`) returns true whenever
`paths[clientNum].count` is nonzero, so once a supported plan exists the
priority-supported call owns the frame and returns before Slot A is reached.
Slot A therefore never runs in exactly the situation a flight window applies to.

Two further facts point the same way. A baked window's landing ranges are
expressed in *published path arclength*, so construction needs the live
`bot2_path_t` and its follower; and the receiver has to be re-seated into that
same follower on exit. Both live inside the supported adapter.

So the adapter joins the published plan where the discovery bench joins it, in
`ai_bot2_supported.c`, before the ordinary `Bot2_PathCommand`. The eighth
constraint below is also narrower than written: `Bot2_ReceiverCommand` drives
the exit tail of the *point* controller, but for a flight candidate
`Bot2_FlightCommand` already owns the tail and its `EXIT` has itself required
two supported observations and receiver acceptance, so the adapter goes straight
to `Bot2_ReceiverResume`, exactly as the bench does.

### The split, and what each half costs

| Site | Work |
|---|---|
| `Bot2_FlightAdapterBeforePmove`, at the Pmove boundary | Root admission, receiver binding, window construction, the two 64-step ground prefixes, and one 256-step rollout per ranked candidate |
| `Bot2_FlightAdapterCommand`, inside the supported adapter | One command per frame, the landing hint, and the follower handoff on exit |

Preparation must happen at a Pmove boundary because `Bot2_ForecastRun` requires
a real `pmove_t` root. That costs one frame of latency between preparing and
owning, which is safe: root admission fires up to 48 units before the approach
point and the run-up is a further 128 units, while the flight kernel re-checks
the actual takeoff pose and refuses atomically if it has drifted.

The prefixes gate the expensive half. A window whose ground preparation yields
no valid takeoff boundary spends **zero** rollouts, and a refused entry backs
off for a second rather than retrying every frame.

### What it deliberately does not do

- **No point candidates.** Ids 8-15 need the legacy support polygons and the
  point receiver. Two baked windows are point-only (`red:f00:v0` needs 14,
  `red:f00:v1` needs 11); the adapter declines them and ordinary path following
  continues. Ordinary travel (id 16) is likewise a legitimate baked answer and
  is declined rather than forced.
- **No native receiver preparation.** The bench samples the receiving line with
  traces as extra evidence. The adapter relies on the rollout instead, which is
  real Pmove and whose `EXIT` requires two actual supported observations.
- **No reduced validation.** Every candidate is still checked by a native
  rollout before it may run. The artifact shrinks the search from 17 to at most
  four; it does not remove the check.

### Evidence

`tools/strafe_flight_adapter_check.py` compiles the real adapter with the real
loader, window construction and transition validator, mocking only the
filesystem, cvars, forecast oracle, receiver bind/resume and flight controller:
**34 scenarios pass.** They cover opt-in gating, an armed experiment keeping its
client, each artifact refusal, root admission, bind and build refusals, prefix
gating, ranked fallthrough (candidate 2 refused, 3 taken), declining ordinary
travel, byte-exact command restoration on refusal, follower handoff on exit, a
refused resume, republication, every gate failure and the retry backoff.

A mocked branch is not a native tested branch. This establishes control flow,
not that a baked candidate is useful at a real entry.

## Open, and why it is not yet answered

The whole penalty analysis above is **offline arithmetic over existing captures**.
It predicts that a ranked list holds the K03 gate; it does not establish it. Two
specific things remain unproven and neither can be settled by more offline work:

1. **Entry-state transfer.** Those candidate records come from runs entered via a
   discovery-driven approach. A production run-up may arrive differently, so the
   measured usefulness may not transfer. This is the guide's "validate reduced work
   against the full native oracle on actual held-out states".
2. **Coverage of thinly observed windows.** 19 of the 31 baked windows have fewer
   than four runs, and 9 have exactly one. Their single-candidate lists rest on one
   observation. They are baked because they executed, not because they are proven
   stable.

Next: held-out native validation and cost. The adapter now exists and is the
instrument for both, but neither has run. Until a bot executes a baked window in
a live match, the ranked lists remain a prediction from offline arithmetic.

## Implementation constraints found while mapping the subsystems

Recorded because each would produce broken code and none is obvious from the README:

- **Bake `firstIndex=1`, never the discovered value.** `Bot2_ReceiverResume` refuses
  when `max(1,state->index) < r->firstIndex` (`bot2_receiver.c:123-125`), and
  `Bot2_ReceiverBind` bakes the bake-time follower index. A stored discovered index
  refuses whenever the live follower is behind it. Also bind `path->count` and
  `path->continuous`, not just the endpoints.
- **`TakeoffBoundary`, `BindWindow` and `SamePublication` are not pure functions of
  their arguments.** They read, and `BindWindow` writes, the file-static
  `connection` struct in `bot2_connectionbench.c`. Only `WindowInterval` and
  `PathHash` are. All three must be re-parameterised before per-client use.
  **Done:** they now live in `bot2_flightplan.c` and the bench delegates to them.
  `tools/strafe_flightplan_check.py` compiles the pre-extraction versions beside
  the current ones and requires byte-identical output, including on the refusal
  paths where construction deliberately leaves partial writes: **58,752
  constructions agree** (1,814 bound, 1,608 of those through the seam-coalescing
  branch), plus interval, takeoff-boundary and path-identity agreement.
- **Save and restore the `usercmd_t` around any yield.** `ai_bot2_route.c:336/346`
  is the only save/restore in the dispatch chain, and the route mutates
  `cmd->generic_cmd` at `:243` *before* its own gate. An adapter that writes a
  command and then releases corrupts the next adapter's frame.
- **Publish the landing hint on every owned frame, not on release.** The route does
  this at `ai_bot2_route.c:363`, immediately before returning true. `Bot2_MoveLayersReset`
  memsets `landingHints[clientNum]`, so a hint published during a reset is erased.
- **`Bot2_MoveLayersReset/LandingHint/Navigation` live in `bot2_movecontrol.h:22-25`**,
  not `ai_bot2_internal.h`.
- **`ai_bot2_plan.c`'s budget is off by default.** `bot_jumpPlanMode` defaults to
  "0", so `plan.limited` is false and `Bot2_PlanStep()` returns true unconditionally.
  An adapter relying on it is unbounded in stock configuration. The per-adapter
  precedents are `ai_bot2_supported.c:32` (32 traces, one query per frame) and
  `ai_bot2_route.c:120-121`. Do not join the supported budget: it is reset at
  `ai_bot2_supported.c:372`, after that adapter's own gate.
- **Insert at Slot A**, between priority-supported (`ai_bot2_movement.c:418`) and the
  route (`:420`), behind a cheap priority predicate, and return false when
  `Bot2_StrafeRouteStatus` reports active so an in-flight cached route is never
  stolen mid-guide. **Corrected while implementing:** Slot A never runs while a
  supported plan exists, because `Bot2_SupportedPriority` is true whenever
  `paths[clientNum].count` is nonzero. The adapter joins the published plan
  inside `ai_bot2_supported.c` instead; see "The production adapter" above.
- **`Bot2_ReceiverCommand` (`bot2_receiver.h:36`) drives the exit tail**; call
  `Bot2_ReceiverAccept` first, since Accept emits a command and never a completion.
  **Narrower than written:** that is the *point* controller's tail. A flight
  candidate's tail belongs to `Bot2_FlightCommand`, whose `EXIT` already required
  two supported observations and receiver acceptance, so the flight path goes
  straight to `Bot2_ReceiverResume`.
