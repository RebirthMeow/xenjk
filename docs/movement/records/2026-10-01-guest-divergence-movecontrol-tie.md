# Guest-trace Class C divergence: MoveControl tie-sensitivity, not a yaw bug (2026-10-01)

Follows [errand falls library test](2026-10-01-errand-falls-library-test.md) (main checkout, uncommitted there) and
[Golan refit, pedestal mines, jitter](2026-10-01-golan-refit-mines-jitter.md), "Defender view jitter" section. Task:
determine the root cause of Class C `GUEST_DIVERGE` misses (the movement's own live command differs from its
rollout, `raw_cmd==last_cmd`) - 30 of 122 missed landings, 19 of them run-up stage 0 on the ground, frames 1-2 after
the commit, with the live executor's yaw differing from the rollout's by 18-27 degrees at equal executor times.

## Hypotheses checked and refuted

Two suspects from the evidence (a one-frame live/rollout offset at `StraightCommit`/`Reissue`, and the guest's
`if(!x->walking)x->yaw=ps->viewangles[YAW];` reseed in `HopStep`/`WalkStep`, `bot2_move.c` around lines 987 and 1072,
reading the real view where the rollout reads its own previous command yaw) do not hold for this class:

- `GuestTraceRecord`/`Bot2_ForecastRun` (`bot2_forecast.c`) copy the root `playerState_t` into a private
  `forecastClient`/`forecastEntities` snapshot (`forecastClient.ps=*r->root->ps;`) before simulating; the real
  `ent->client->ps` is read once and never written back. `RunCandidate`'s diagnostic rollout at commit
  (`GuestTraceRecord`, called before `Reissue` in `StraightCommit`) cannot corrupt the live state Reissue then reads.
  Frame 0 (the commit frame itself) never mismatches in the retained worlds, consistent with both sides starting
  from the same `before` executor and the same real `ps`.
- Re-running `followup.py` against the retained worlds (`.codex-research/hopmiss-20261001-a/trace-1..8`) with the
  existing `ex_walking`/`pred_ex_walking` fields shows `walking 1/1` in all 19 ground/stage-0/ex_yaw-differs cases:
  the live executor never clears `walking` and never hits the view reseed. The walking-continuity hypothesis is
  refuted by the diagnostic's own retained output; no rebuild was needed to settle it.
- A supplementary pass (`nearest_check2.py`, same worlds) pulled `dpos`/`dv`/`nearest_dist` for the 19 cases: position
  stays within 0.0-1.9 units (well under the 4-unit match tolerance) while velocity differs by 30-90 u/s (over the
  25 u/s tolerance) - the trigger is a velocity disagreement, not a position one. `nearest_dist` ranges widely (33 to
  over 1,600 units), so a nearby guest's moving collision volume is not a uniform explanation either (it may
  contribute to the handful under 100 units, but not to the ones past 300-1,600).

## Root cause: the same MoveControl tie-sensitivity the owner already diagnosed and declined to fix

`Bot2_MoveControl` (`bot2_movecontrol.c`) re-derives its (key, view) from scratch every frame by scanning a 5-degree
grid of candidate angles and 9 move keys and picking the minimum-error candidate; nothing about the choice is
carried from the previous frame except the turn-rate clamp. The "Defender view jitter" section of
[2026-10-01-golan-refit-mines-jitter.md](2026-10-01-golan-refit-mines-jitter.md) already measured this directly
(`.codex-research/aimjitter-20261001-a/`): with a small intent velocity, the +9 and -9 degree turn-limited views tie
and the controller alternates between them frame to frame, independent of any aim/view writer - "the movement alone
wrote the view... the same writer on both frames" for about three of four flips. The owner measured several
mitigations (hold-last-view, view-along-velocity) and declined the controller-level fix: "What is left is the
movement controller's own forward/brake alternation; the owner declined the controller-level fix for now (it needs
the runner floor)."

The Class C run-up cases in `hopmiss-20261001-a` are the same mechanism at a different moment: a guest's real Pmove
trajectory and the frozen single-agent rollout recorded at commit agree on position to under 2 units but drift apart
in velocity by enough (30-90 u/s) to land on the other side of a near-tied `MoveControl` candidate, which then
commits to a different key/view for a couple of frames (18-27 degrees, matching two 25 ms steps at the 360 deg/s turn
clamp) until the turn-limited search reconverges. `HopStep`'s ground run-up passes an unchanged `hop->dir` as intent
on both sides (same committed `bot2_movePlan_t`), so the only way the two sides can choose different keys is this
tie-sensitivity reacting to the small, otherwise-within-tolerance velocity difference - not a logic defect in the
executor's stage/walking state, which the data already shows was identical.

## Guest vs. runner

`Bot2_MoveControl` and `HopStep` have no `MoveGuest` branch; this sensitivity is shared, unconditional code. It is
visible for guests only because `GUEST_DIVERGE` is the one diagnostic that continuously compares a committed guest's
live execution against its own recorded rollout; the runner has no equivalent post-commit trace (only pre-commit
parity on search candidates, which disarms on a world-state mismatch rather than reporting one). There is no new
evidence here that the runner's measured floor is affected, and the owner has already deferred any
`MoveControl`-level change pending a runner floor run. Per that standing decision and the task's own constraint, no
runner-affecting change is made.

## Disposition

Not a bug in the guest executor's yaw/walking continuity (refuted directly from retained evidence) and not a trace
artifact (the frame-k alignment and the match tolerance are both working as documented; the comparison correctly
flags a real, if small, behavioral divergence). It is the already-diagnosed, owner-declined `MoveControl` tie
sensitivity, observed here during run-up commits rather than steady walking. No `bot_moveGuestCommitYaw` cvar was
added: pinning the guest's commit-frame yaw would not touch this path (frame 0 already matches byte for byte; the
divergence appears one to two frames later, inside the ordinary per-frame `MoveControl` re-scan, not at any
commit-time seed), and a synthetic per-frame override would mask the controller's real tie-break rather than correct
it. No source changed, so no new native build or smoke campaign was run; `classes.py`/`followup.py` against the
existing `hopmiss-20261001-a` worlds were sufficient to confirm and refute the candidates. If the owner wants the
residual miss rate down, the lever is the same one already on record: a hysteresis/tie-break change in
`Bot2_MoveControl`, gated and measured on the runner floor.
