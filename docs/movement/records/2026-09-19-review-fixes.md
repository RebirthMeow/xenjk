# Review repair work order, 2026-09-19

Status: **closed** by [the repair record](2026-09-19-review-repairs.md),
2026-09-19. All four findings are repaired, each with a regression shown to
fail against a paired defect-control build (or against the retained evidence)
before it passed. The text below is the work order as written, kept unchanged
so the repairs can be read against what was asked for; read the repair record
for what was measured and for the two deviations it records.

The [follow-up review](2026-09-19-repair-follow-up.md) adds two bounded
lifecycle/probe items. The next substantive milestone is the
[refined independent-search method](2026-09-19-refined-method.md); the old
closing instruction to resume demonstrations first is superseded.

---

Status when written: **four open P2 findings; no implementation fixes or
native validation performed by this review**. The review inspected the working changes and used
an in-memory Python probe for the first-command conversion. This document is
the next repair milestone, before new route qualification or interpretation
of the affected search experiments. It does not establish that every retained
run was affected or replace the historical measurements with new results.

The owner requested documentation of the fixes. The documentation update does
not itself implement them. Resume through [HANDOFF.md](../HANDOFF.md).

## 1. Reset candidate results on every search

Entry points: `StraightSearch`, `ExploreState`, `StraightJudge` and
`straightResult_t` in [bot2_move.c](../../../codemp/game/bot2_move.c).

Both search callers reuse static result storage. Ordinary candidate
construction clears the embedded plan, outcome and variant, but leaves
`rerouted`, `tapOnward`, `routeCells` and other result fields from the previous
use. Once an extension sets a flag, a later ordinary candidate in that slot
can read the extension walk reference instead of its own. With no extension
walk in that search it becomes unjudged; with one, it uses the wrong
reference. Stale route metadata can also reach route installation. The
storage is shared across calls and clients, not reset by `Move_Reset`.

Repair: give every populated result slot a complete, explicit initialization
before construction or judging. Preserve the newly constructed candidate and
its intended variant if initialization happens inside `StraightJudge`.
Initialize result-only sentinel fields consistently; do not rely on static
zero initialization or clear only the flag that happened to expose the bug.
Cover ordinary, planned, heading-sweep and exploration candidates.

Required regression: reuse the same slot across searches, first with a
reroute or tap continuation, then with an ordinary admitted candidate. Exercise
both extension flags separately, with the feature still enabled but no rescue
needed on the second search, and with it disabled. The second result must use
its own arm's reference, carry no old route/tap metadata and match a fresh
slot's decision. Include changed candidate counts/roles and a client reset.
This must test the real initialization/judging path, not a duplicate model.

## 2. Preserve the first command's actual physics duration

Entry points: `tape_entries` and `tape_text` in
[route_asset_build.py](../../../tools/route_asset_build.py), and the command
loop in [bot2_route_play.c](../../../codemp/game/bot2_route_play.c).

The converter always writes `dt=0` for the first command. Playback substitutes
`pmove_msec`, discarding that command's recorded duration. The review probe
used `pre_cmd_time=100`, `cmd_time=132`: it produced zero instead of 32 ms.
Under an 8 ms fixed step the first acceleration/jump command therefore receives
only one step instead of four.

Repair: derive the initial duration from the recorded pre-command state and
command time. Check `post_cmd_time` and the native fixed-step rounding/clamping
path so that subsequent deltas reproduce the actual accepted Pmove duration
as well. Define handling of missing, zero or invalid timing explicitly;
do not silently invent an 8 ms physics step. Keep command elapsed time and the
recording's server-level elapsed time distinct.

Required regression: extend
[test_route_asset_build.py](../../../tools/test_route_asset_build.py) with
first-command durations of 8, 16 and 32 ms, plus a first jump and a later gap.
Add `pre_cmd_time` to the test fixtures and replace the assertion that the
first delta must always be zero. Verify native
pre/post state for the first replayed command against its source row, and
then verify the full sequence. Check fixed-step alignment and zero-duration
handling explicitly. Rebuild assets into a fresh directory from original
CSVs; retain existing assets and approvals as historical evidence.

## 3. Bind checkpoints to the command that produced them

Entry points: `contract` / `tape_text` in
[route_asset_build.py](../../../tools/route_asset_build.py), `Marks`,
`FollowCorrection` and `Bot2_RoutePlayClient` in
[bot2_route_play.c](../../../codemp/game/bot2_route_play.c).

The playback loop may execute several commands in one server frame. After
each one it calls `Marks` with `level.time - tape.startTime`, so the first
command can consume checkpoints belonging to commands not yet executed.
Their drift is measured against an earlier position and cannot be corrected
later. This can inflate approval drift or trigger a follower divergence abort.
Using server elapsed time for follower correction has the same alignment risk.

Repair: carry a stable source-row/command association into each contract mark
and evaluate that mark after its matching command. Distinguish marks taken
after Pmove from any post-touch state as the recorder does. Server-frame
timestamps alone cannot distinguish multiple recorded commands in one frame;
changing only the caller's clock is insufficient unless that mapping is
unambiguous. Align the follower's reference with executed tape progress too.
Preserve native pickup time as a separate outcome measurement.

Required regression: a fixture with several commands and checkpoints due in
one server frame, including equal server timestamps. Each mark must be read
exactly once at its associated command, with expected drift independent of
how commands are batched. Include a slower/catch-up frame and both playback
and follower modes. Preserve every original checkpoint and drift threshold.
If the tape/asset format changes, version it and reject unsupported older
formats clearly or provide an explicit conversion from the retained CSV;
never silently reinterpret existing approvals.

## 4. Release AI ownership when arming is refused

Entry points: `Arm`, `Hold`, `Finish` and the stop command in
[bot2_route_play.c](../../../codemp/game/bot2_route_play.c).

`Arm` calls `Hold` before its remaining validations. With `bot_routePrefix=1`
and `bot_moveRewrite=0`, it returns false after assigning `customThink=Idle`.
`tape.armed` remains false, so `Finish` returns immediately and stop cannot
release the bot. The follow-start offset refusal also occurs after `Hold`.

Repair: finish validation before taking ownership wherever possible. Any
validation requiring placement must have a defined rollback on failure,
including AI ownership and placement side effects. Track the client actually
held; do not use a stale `tape.client` to release another bot. Restore the
previous owner where appropriate rather than blindly replacing it. Audit
load/re-arm/stop transitions for the same ownership lifetime while making
this change.

Required regression: refuse a prefix with rewrite disabled, refuse an
out-of-range follower start, and call stop after each. The original bot AI
must resume, with no active playback or held client left behind. Then arm a
valid tape and stop it, verifying normal ownership release. Include an active
tape replacement/re-arm check so refusal cannot strand either client.
Use an isolated console or a dedicated harness for the refusal settings;
`route_approve.py` has no arbitrary `--set` passthrough.

## Verification and completion

1. Add focused regressions that expose the four defects, implement the
   repairs, and run affected Python and native checks. Test helpers must
   retain or archive artifacts: the asset suite currently calls `unlink`,
   and replacing `TemporaryDirectory` alone does not prevent those deletions.
2. Build into a fresh unique directory using the
   [verification reference](../../archive/strafe-movement-2026-09-14/STRAFE_MOVEMENT_VERIFICATION.md).
   Freeze measured sources during build/collection and bind source hashes,
   binaries, settings, map, case, seed and regenerated asset hashes.
3. Run focused repeated-search extension cases and playback timing/ownership
   cases first. Then check the default 36-case movement floor and seeded
   12-case level-1 arm with the established configuration (`--edges --parity
   256`, seed `move-explore-20260916-b`). Preserve failures and report parity,
   deaths, forbidden spend, completion and ownership separately. A passing
   single floor does not erase the recorded stochastic failure rate.
4. Re-run the three retained routes from their own starts and through the
   benchmark-stand prefix, with matched repetitions and unchanged approval
   thresholds. Report first-command state, per-command checkpoint alignment,
   drift, actual pickup, prefix and total time. Keep follower qualification
   separate; these repairs do not establish that its steering works.
5. Compare repaired controls with affected reroute/tap experiments before
   carrying forward causal or ranking conclusions. Retained route timings and
   drift remain historical outputs until regenerated assets and corrected
   measurement are checked. Do not claim all prior approvals are invalid
   solely from this review, or claim new qualification without the checks.
6. Record results and failures in a new dated evidence record; update the
   handoff, plan and evidence/scorecard only as results warrant. Mark each
   finding fixed only with its regression result. Keep experimental defaults
   off and make no installed-game changes as part of this repair milestone.

After these checks, resume the requested respawn demonstrations and contention
work in the handoff. The older chain-entry and onward-held defects remain
open; this review does not establish their cause or resolve them.
