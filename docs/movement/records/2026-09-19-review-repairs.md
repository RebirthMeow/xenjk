# The four review repairs, measured

Subsequent [follow-up review](2026-09-19-repair-follow-up.md) corroborates the
repair evidence and records two additional lifecycle/probe issues. Its scope
corrections matter: one floor attempt is incomplete on setup; the ordinary
valid-arm probe phase did not observe an arm; the sub-0.8 handoff bound below
is per along/cross component, not radial displacement. The original measured
records are retained. Current next work is the
[refined independent-search method](2026-09-19-refined-method.md).

The [review repair work order](2026-09-19-review-fixes.md) named four open
defects. All four are repaired, each with a regression that was first shown to
**fail against the defect** and then to pass against the repair. Two of them
were latent in the retained evidence and two were not:

- **Candidate result reuse (finding 1) was live.** With the extension passes
  armed, a repaired build reports **0** dirty result slots across three cases;
  a paired build with only that defect put back reports **372, 396 and 200**,
  and most of the dirty slots carried a `rerouted` or `tap_onward` flag from a
  previous search - the consequential case, where an ordinary candidate is
  judged against a walk reference it never rolled.
- **Checkpoint alignment (finding 3) was live, and it was inflating drift.**
  Re-running the three retained routes on the repaired build reproduces
  **every recorded route time exactly** and drops max drift from **36.984,
  37.832 and 23.791 to 0.346, 0.294 and 0.322** on the controls, and from
  **15.357, 19.066 and 14.230 to 0.896, 0.971 and 0.286** over three
  benchmark-stand repetitions each. The replays were already reproducing the
  recordings; the drift was the measurement reading marks early.
- **First-command duration (finding 2) was real but latent in these three
  assets.** Their first recorded command is an 8 ms one, which is exactly what
  playback substituted, so no retained route timing was affected. The
  converter probe in the work order (`pre_cmd_time=100`, `cmd_time=132`) is
  reproduced as a unit regression and now yields 32 ms instead of 0.
- **Refused-arm ownership (finding 4) was live.** A repaired build releases on
  every refusal; the paired defect build leaves the client held after `stop` on
  both refusals tested.

Nothing here re-qualifies the follower, and no experimental default changed:
`bot_routePrefix`, `bot_routeFollow`, `bot_moveReroute` and `bot_moveTapOnward`
are all still off. The two older defects - chain entry and the onward-held
dead-end landing - are untouched and still open.

## What changed

### 1. Every result slot is emptied before it is built in

`StraightSearch` and `ExploreState` keep their candidate results in `static`
storage, shared across searches and across clients and never touched by
`Move_Reset`. Construction cleared the embedded plan, the outcome and the
variant; everything else was the previous use's.

`StraightResultReset` now empties the whole slot, and sets the sentinels rather
than assuming them from the zeroing: the measure's frame indices through a new
`SpikeMeasureClear` (shared with `SpikeMeasure`, so "never measured" means the
same thing everywhere), `walkFrame` to -1, and `tapLaunch`/`tapLand`/`tapCell`
to -1 as `TapMeasure` leaves them when nothing tapped. All four construction
paths use it: ordinary, planned-state, heading-sweep and exploration.

The judge audits what it is given. Any slot that still carries a previous
search's fields is named on a `[STRAIGHTSLOT]` line and emptied before it is
judged, and the running count appears as `stale_slots=` on the plan line. A
build whose constructions all reset reports zero for the life of the map, so
the audit is the regression's observable and a construction path that stops
resetting cannot go quiet.

### 2. The first command carries its own physics duration

Pmove runs from `ps->commandTime` to `cmd.serverTime` in steps of at most
`pmove_msec`, and it does not discard the remainder: the duration is the whole
difference, clamped to the one second Pmove allows. The recording carries both
ends - `pre_cmd_time` going in, `cmd_time` on the command - so
`route_asset_build.command_ms` reads the duration from the row instead of
inferring it from the previous row's command time. The first row has no
previous row, which is exactly why it used to get a zero.

Missing, zero and invalid timing are named rather than approximated. A row
without `pre_cmd_time` falls back to the previous command time and only when
there is one; a first row without either is refused; a duration of zero or less
is refused, because Pmove runs no physics for it. Nothing substitutes a nominal
step, at either end: playback's `c->dt>0 ? c->dt : tape.pmoveMsec` is gone and
`Load` refuses any tape whose command delta is outside 1..1000 ms.

### 3. A checkpoint is read after the command that produced it

The playback loop can run several commands in one server frame. `Marks` was
called after each one with `level.time - tape.startTime`, so the first command
of a frame consumed every mark due in that frame - measuring their drift
against a position the replay had not reached yet, with no later correction.
In the three retained recordings **17 of 51, 21 of 61 and 25 of 73 contract
marks are not at the first command of their server frame**, and the recordings
average 3.1 commands per frame.

The contract now carries the command index each mark was recorded after, and
`Marks` consumes marks by that index. The alignment no longer depends on how a
replay batches commands into frames. `elapsed_ms` stays on the mark and on the
telemetry, for reporting, beside the tape's own accumulated physics time
(`tape_ms`); the recorder's post-Pmove snapshot is what a mark is taken from,
as before, never its post-touch one. The follower's segment interpolation moved
to the same axis, from the server clock to executed tape progress.

The pickup time and the run's elapsed time are still the server's, reported
separately in `DONE`, because that is what they are.

### 4. Arming validates before it takes the AI, and rolls back if it cannot

`Arm` took the AI with `Hold` and then ran the rest of its validations. A
refusal after that point - `bot_routePrefix 1` without `bot_moveRewrite`, a
follower start out of range - left the bot idled with `tape.armed` still false,
and `stop` acts on an armed tape, so nothing could release it.

Everything decidable without touching the bot is now decided first. The hold,
and the placement the start check needs, happen last and are undone together:
`Place` saves the player state it overwrites and `Unplace` restores it, and
`Release` gives the think back. Ownership is tracked on its own - `heldClient`
and the think that was there - rather than read from `tape.client`, which a
refused arm never sets and which would otherwise name a different bot. `Finish`
releases, including when it is called with nothing armed, which is what makes
`stop` work after a refusal. An arm over a running tape ends that tape first.

## The regressions, and what they do against the defect

Each is paired against a **defect-control build**: the repaired tree with
findings 1 and 4 put back and nothing else changed, so the two builds differ
only in the defect. Findings 2 and 3 are paired against the retained evidence
instead, which is the "before" for them.

### Finding 1: `stale_slots` on the real judge

Three cases, extension passes armed (`bot_moveReroute 3`,
`bot_moveTapOnward 1`, `bot_moveHeadingSweep 1`, `bot_moveReplay 2`) so the
candidate count and the roles change between searches, the established
`--edges --parity 256` with the `move-explore-20260916-b` seed, one fresh
world per case:

| | red_out_L1_stand | red_out_L2_stand | blue_out_L1_stand |
| --- | --- | --- | --- |
| repaired, stale slots | 0 | 0 | 0 |
| defect control, stale slots | 372 | 200 | 396 |
| of those, flagged `rerouted` or `tap_onward` | 185 | 72 | 288 |

Both arms passed all three cases. Their times are not a comparison - one
repetition each, and the floor is stochastic - but the defect arm's
`blue_out_L1_stand` is worth recording as an observation rather than a claim:
84,675 ms with 64 `edge:held` events, against 18,150 ms and none on the
repaired arm.

The work order asked for the two extension flags exercised **separately** and
with the feature disabled, so three more paired arms were run over
`red_out_L1_stand` and `blue_out_L1_stand`, same configuration, one flag at a
time. Counted from the `[STRAIGHTSLOT]` lines themselves:

| arm | stale slots | of those `rerouted=1` | of those `tap_onward=1` |
| --- | --- | --- | --- |
| repaired, reroute only | **0** | 0 | 0 |
| repaired, tap only | **0** | 0 | 0 |
| repaired, both off | **0** | 0 | 0 |
| defect, reroute only (`bot_moveReroute 3`) | 972 | **197** | 0 |
| defect, tap only (`bot_moveTapOnward 1`) | 648 | 0 | **181** |
| defect, both off | 784 | 0 | 0 |

The flags separate exactly: the reroute arm leaves only `rerouted` slots behind
and the tap arm only `tap_onward` ones, and with both features off the
staleness is real but carries neither flag - the case where the defect is
present and, on this evidence, inconsequential. Every arm passed both its
cases on both builds, with no deaths.

- repaired: `.codex-research/rev1-fix-ext/`, `rev1-fix-reroute/`,
  `rev1-fix-tap/`, `rev1-fix-off/`
- defect control: `.codex-research/rev1-defect-ext2/`, `rev1-defect-reroute/`,
  `rev1-defect-tap/`, `rev1-defect-off/`
- builds: `review-fix-build-d720a91ebc6a48dc9975e263ed5ca487` and
  `review-defect-build-20260919` for the both-flags arm,
  `review-fix-build-d` and `review-defect-build-d` for the three single-flag
  arms
- `bot2_move.c` source hash `4f4d119cd494…` repaired, `79cda9c9964a…` defect

### Finding 2: the converter's own timing

`tools/test_route_asset_build.py` gains `pre_cmd_time` and `post_cmd_time` to
its fixtures and eleven timing cases: first-command durations of 8, 16 and
32 ms; the work order's own probe (100 → 132) asserted to be 32 and not 0 and
not 8; a first jump asserted to get four 8 ms steps rather than one; a 20 ms
first command asserted to stay 20, because Pmove does not round the remainder
away; the one-second clamp; zero, negative and missing timing each refused with
a named reason; a later gap; and the deltas asserted to walk the recorded
command times exactly. The assertion that the first delta is always zero is
gone. 30 tests in the file, all passing; the full suite is 582 tests with one
pre-existing unrelated failure (`test_strafe_map_transitions`, a missing
declarations directory, unchanged by this work).

The suite no longer deletes its recordings: they are written under
`.codex-research/unit-artifacts/route-asset-build/` and kept, because a
converter regression is only diagnosable from the CSV it was given.

The first replayed command was also checked natively against its source row,
on all three routes: the state arming places the bot in matches the
recording's `pre_` columns to 0.0000 units, and the contract mark bound to
command 0 matches its `post_` columns to 0.0000, at `drift=0.000`. The full
sequence follows in the drift table below. All three recordings' first
command is 8 ms, which is the reason the defect changed nothing here.

### Finding 3: marks read at their own command, in a real world

Assets rebuilt into a **fresh** directory from the original recording CSVs -
`.codex-research/route-assets-20260919-fix/`, source `sha256` identical to the
retained assets' - and the retained
`.codex-research/route-assets-20260918/` assets left untouched as the
historical evidence they are. Twelve runs on the repaired build: each route
from its own start (the retained control configuration) and three repetitions
from the benchmark stand `(152,-1160,1924.125)` with the retained arm's gates.

| route | run | retained elapsed / prefix | repaired elapsed / prefix | retained max drift | repaired max drift |
| --- | --- | --- | --- | --- | --- |
| `1789686350-0-c0` | control | 12,900 / - | 12,900 / - | 36.984 | **0.346** |
| | stand ×3 | 12,875 / 2,000 | 12,875 / 2,000 | 15.357, 15.143, 16.156 | **0.896, 0.782, 0.292** |
| `1789686390-1-c0` | control | 14,825 / - | 14,825 / - | 37.832 | **0.294** |
| | stand ×3 | 14,800 / 550 | 14,800 / 550, 550, 650 | 19.066 ×3 | **0.253, 0.829, 0.971** |
| `1789686416-2-c0` | control | 18,575 / - | 18,575 / - | 23.791 | **0.322** |
| | stand ×3 | 18,550 / 450 | 18,550 / 450 | 14.230, 9.606, 9.606 | **0.277, 0.286, 0.286** |

Every route time is identical to its retained value, every hand-off is inside
0.8 units at a standstill, and all twelve runs are approved. The one prefix
time that differs (650 against 550 on one repetition) is the walk's, not the
tape's; the retained record already shows that walk at 775 ms in another arm.

Per-command alignment is visible directly: every `[ROUTEPLAY] MARK` line now
carries `mark_command` and the executed `command`, and across the **740 marks
of all twelve runs they are equal every time**. The first four marks of the
first control run read 0.000, 0.004, 0.005 and 0.011 units of drift.

The follower reads marks on the same axis, so it was run too, on
`1789686390-1-c0` from the tape's own start:

| run | verdict | elapsed | corrections | max drift |
| --- | --- | --- | --- | --- |
| open loop (control) | approved, pickup | 14,825 | - | **0.294** |
| `bot_routeFollow 1`, gain 0 | approved, pickup | 14,825 | 0 | **0.294** |
| `bot_routeFollow 1`, gain 0.30 | rejected, tape_end | 14,825 | 123 | **114.205** |

All 61 marks are read at their own command in each. Gain 0 reproduces the
open-loop control exactly, which is what arming the follower without steering
should do. **Gain 0.30 still does not work, and correct measurement makes that
worse, not better**: the retained record put the follower at 78.3 against
open-loop's 37.8 on this route, a ratio of two; measured properly it is 114.2
against 0.294. The cause is the one that record already identified - the error
signal is cross-track against the chord between marks, which reads the
recording's own curvature as error - and these repairs do not touch it.
`bot_routeFollow` stays default off and unqualified. What changed here is only
the axis the segment is interpolated on, from the server clock to executed tape
progress; the chord is still the chord.

So the reading is: **the retained drift figures were mostly measurement error,
and the retained route times were not affected.** The three routes reproduce
their recordings to a fraction of a unit. The 64-unit approval threshold is
unchanged, and every retained approval that passed it still passes.

### Finding 4: ownership, in a world

`tools/route_ownership_probe.py` stages a world the way the approval runner
does, and runs four phases: the prefix refusal (`bot_routePrefix 1`,
`bot_moveRewrite 0`), a follower start 512 units off the tape's (outside the
64-unit follower tolerance), an arm over a running tape, and an ordinary arm.
After each, `stop`, and then the module is asked what it still owns.

The criterion is ownership, not movement, and deliberately: the fixture's flag
runner drives its bot from its own frame hook rather than through the think
pointer, so a held bot in this world still moves and moving would prove
nothing. `bot_route_play where` was added for this - read-only, so that asking
does not take the AI the probe is watching - and reports `held` and `idled`.

| phase | repaired | defect control |
| --- | --- | --- |
| prefix refused | refused, never held, `held=-1` after stop | refused, **`held=0` after stop** |
| follower start out of range | refused, released at the refusal, `held=-1` | refused, **`held=0` after stop** |
| arm over a running tape | old tape ends `reason=rearmed`, released at stop | released at stop |
| valid arm and stop | released, `held=-1` | released |
| verdict | **pass** | **FAIL** |

- repaired: `.codex-research/rev4-fix-own3/ownership.json`, build
  `review-fix-build-d`
- defect control: `.codex-research/rev4-defect-own2/ownership.json`, build
  `review-defect-build-d`

The probe retries `stop` and reads the state rather than the reply, because an
rcon reply can be lost and a lost reply is not a stranded bot - the first run
of this probe failed on exactly that.

## The floor and the seeded arm

**The seeded 12-case level-1 arm passes 12/12** in the established
configuration (`--edges --parity 256`, seed `move-explore-20260916-b`), with
zero parity mismatches, zero `judged_short` and `stale_slots=0` throughout:
`.codex-research/rev-seeded-L1-20260919/`.

**The first five floor runs were off-configuration, and are retained as such.**
Three were the bare default and two added `--edges` alone; all five report
`straight_commits=0`, because the straight search's candidates are rolled out
through the parity harness and `--parity 0` means it never rolls one out. A
floor in that mode exercises almost nothing this repair touches. This is the
same off-configuration error the handoff already lists among the four retained
measurement mistakes, repeated here:

| run | configuration | commits | deaths |
| --- | --- | --- | --- |
| `rev-floor-20260919` | default | 0 | 3 |
| `rev-floor-20260919-r2` | default | 0 | 4 |
| `rev-floor-20260919-r3` | default | 0 | 2 |
| `rev-floor-edges-fix-r1` | `--edges` | 0 | 4 |
| `rev-floor-edges-fix-r2` | `--edges` | 0 | 1 |
| `rev-floor-defect-r1` (defect control) | default | 0 | 0 |
| `rev-floor-defect-r2` (defect control) | default | 0 | 1 |
| `rev-floor-edges-defect-r1` (defect control) | `--edges` | 0 | 1 |
| `rev-floor-edges-defect-r2` (defect control) | `--edges` | 0 | 2 |

Both builds are in that table and they straddle each other - repaired 3, 4, 2,
4, 1 deaths and the defect control 0, 1, 1, 2 - which is what a mode neither
build's changed code runs in should look like. It is not evidence about the
repair either way, and it does say that this fixture's L1 respawn cases die
readily when the search is not running: every failure in all nine runs is an
L1 respawn case, never a stand and never level 2 or 3.

The retained population the "10 %" figure comes from is narrower than the
handoff states: of the 36-case floors on disk, **44 are `--edges --parity 256`
with the search actually committing** (390-476 commits a floor), and **40 of
those 44 are clean**, three have one death and one has two. That is the
population a floor of this work has to be compared against, and it is the
configuration run below.

**In the standard configuration the repaired build is clean.** Three floors,
`--edges --parity 256` with the `move-explore-20260916-b` seed, the search
committing 410-428 times a floor as the retained population does:

| run | cases | deaths | parity mismatches | `judged_short` | commits | stale slots |
| --- | --- | --- | --- | --- | --- | --- |
| `rev-floor-std-fix-r1` | 35/36 | 0 | 0 | 0 | 410 | 0 |
| `rev-floor-std-fix-r2` | 36/36 | 0 | 0 | 0 | 422 | 0 |
| `rev-floor-std-fix-r3` | 36/36 | 0 | 0 | 0 | 428 | 0 |

`rev-floor-std-fix-r1`'s missing case is a **setup failure, not a bot
failure**: `blue_out_L3_respawn5` ended with `corner start refused: None`, a
lost rcon reply before the case began, and it is retained as one. No case in
any of the three died. `red_out_L1_stand` ran 19,150 and 20,700 ms, inside the
retained 19,150-36,200 range.

Three clean floors are not proof of safety - against a population of 40 clean
in 44, three clean is an ordinary draw - but they are the evidence the floor
gate asks for, and they are in the configuration that runs the repaired code.

The paired defect build reports **396 to 428 stale slots per world** on the
same floor, where the repaired build reports zero. Worth being exact about what
that means: with the extension passes off, as the standard floor has them,
**none** of those stale slots carries a `rerouted` or `tap_onward` flag - the
staleness is in the margin, walk, score and measure fields, which the judge
overwrites before it reads them. The defect is present in every search; it is
only consequential where an extension pass has run, which is what the dedicated
arms above measure. Neither fact excuses the other.

The defect build's own three standard floors are recorded beside them and are
the same floor by every outcome measure - 36/36, no deaths, 428, 428 and 429
commits, no parity mismatches, `red_out_L1_stand` at 19,600 ms - which is the
expected result and the reason the extension-armed arms, not the floor, are
finding 1's regression.

## Two deviations from the work order

Both are recorded because the work order asked for something the fixture
cannot give.

**Finding 1's regression is an audit over the real search, not a scripted slot
sequence.** The work order asked for a test that reuses one slot across
searches with each extension flag in turn. The flags are exercised separately
and together, and with the feature disabled, in the six paired arms above. There is no native unit harness in
this repo, and a scripted model of the judge would be the "duplicate model" the
work order rules out, so the check was put where the real slots are: the judge
audits every slot it is handed, on every search of every case, and the count is
reported. That covers strictly more slot reuses than a scripted sequence would,
and the flags are exercised separately and together in the arms below. What it
does not do is assert a *specific* second-result decision against a fresh
slot's; it asserts that no slot arrives dirty, which is the condition that
decision depended on.

**Finding 4's criterion is ownership, not resumed movement.** The work order
says the original bot AI must resume. In this fixture it never stops: the flag
runner drives its bot from its own frame hook rather than through the think
pointer, so a held bot still moves and movement cannot tell a released client
from a stranded one. The probe reads the module's own ownership instead - no
tape armed, no client held, the think no longer this module's idle - and
reports the movement beside it without judging on it. The first version of the
probe judged on movement and passed the defect build; that is how this was
found.

## Format versioning

The tape is now `BOTROUTE2` and the asset `BOT_ROUTE2`. This is not an
addition: the first command's duration and the marks' command index are
corrections to fields that already existed, so the same tokens mean different
things. `Load` refuses `BOTROUTE1` by name and says what to do about it, rather
than reinterpreting an asset that was approved under the older meaning.
Rebuilding from the retained recording CSV is the conversion, and it is what
produced the assets used here.

The rebuilt tapes' `duration_ms` is 8 ms longer than the retained ones in all
three cases, which is the first command's real duration where a zero used to
be - and 8 ms is also what playback substituted, so the physics is unchanged.
That is why finding 2 changed no retained route time.

## Limits

- One map, one fixture, an empty world, and the route comparison spans the same
  two physics settings the prefix record already flagged.
- The defect arms are one repetition per case. They establish that the
  regressions detect the defects; they do not measure what the defects cost,
  and the 84,675 ms run is one sample of a stochastic metric.
- Finding 1's repair is a correctness repair. Whether any *retained* search
  result was changed by the stale flags is not established here, and the
  reroute and tap experiments should be compared against repaired controls
  before their causal or ranking conclusions are carried forward.
- The follower is unqualified, and these repairs say nothing about its
  steering. Its error signal is still measured against the contract's chords,
  and on the line it is now measured at 114.2 units of drift against open
  loop's 0.294.
- The floor is stochastic and a handful of runs decides little either way. The
  retained standard population is 40 clean floors in 44; a single clean floor
  is roughly a 90 % draw from it.

## Where the worlds are

| what | where |
| --- | --- |
| repaired build (routes, ownership) | `.codex-research/review-fix-build-d` |
| defect-control build (ownership) | `.codex-research/review-defect-build-d` |
| repaired build (search arm) | `.codex-research/review-fix-build-d720a91ebc6a48dc9975e263ed5ca487` |
| defect-control build (search arm) | `.codex-research/review-defect-build-20260919` |
| rebuilt assets | `.codex-research/route-assets-20260919-fix/` |
| retained assets, untouched | `.codex-research/route-assets-20260918/` |
| search arms | `.codex-research/rev1-fix-ext/`, `.codex-research/rev1-defect-ext2/` |
| route runs | `.codex-research/rev23-routes-20260919/` |
| ownership probes | `.codex-research/rev4-fix-own3/`, `.codex-research/rev4-defect-own2/` |
| single-flag arms | `.codex-research/rev1-{fix,defect}-{reroute,tap,off}/` |
| follower runs | `.codex-research/rev3-follow-g0/`, `.codex-research/rev3-follow-g030/` |
| seeded level-1 arm | `.codex-research/rev-seeded-L1-20260919/` |
| standard-configuration floors | `.codex-research/rev-floor-std-fix-r*/`, `.codex-research/rev-floor-std-defect-r*/` |
| off-configuration floors, retained | `.codex-research/rev-floor-20260919*/`, `.codex-research/rev-floor-edges-*/`, `.codex-research/rev-floor-defect-r*/` |
