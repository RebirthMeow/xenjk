# Bot2 structure work: a plan for between campaigns (proposal, 2026-11-09)

A proposal, not results. It covers the structure items of the [2026-11-09 Bot2 review](../../2026-11-09-bot2.md):
BA-18 (`bot2_defender.c` split), BM-19 (executor ownership and size), BA-20 and BM-19's per-client statics, and
BM-18's dead state. Nothing here is built. Each step is meant to replay the worlds before it exactly
(`tools/laps_identity.py` on Kejim and Dantooine seed 3, plus one standoff world, Dantooine seed 4), so it can run
between outcome campaigns without a laps pair. Line numbers are from 6fe7bb2f; the 2026-11-09 follow-up branch
(`claude/bot2-review-next`) has already moved some of the code named below.

## Why between campaigns

Each step touches files that every campaign build compiles. A move that is lossless still changes the source hashes
every build binds to, so it should land when no campaign is collecting and no arm is waiting for a reused control.
The work is also easier to review in pieces than mixed with behaviour changes: each step below is one commit that
changes no decision.

## Order

1. **Per-client reset contract first** (BA-20, BM-19's statics). It changes no layout and makes the later moves
   safe.
   - Add `Bot2_ClientReset(int client, int reason)` (reasons: free slot, respawn, team change, map change) in
     `ai_bot2.c`, and a small registry: each module that owns per-client statics registers one reset function.
   - Register what the review lists: `aimView[]` (bot2_aim.c), `runnerCapturesSeen[]`, `meleeSince[]`,
     `dropChecks[]`, `runnerCamp[]`, `skRequest[]` and the new `skHonour[]`, `goalChainIndex[]`, `defenders[]`;
     in the executor `padFlight`, `moveForbid`, `routePending`, `guestPredict`, `guestTrace`, `dropDeferred`,
     `poolMove`, `poolOwed`, `split`, and `kicks` (bot2_kick.c). Also the 2026-11-09 additions (`stepOffMemo`).
   - Each reset does what the module's own keyed check does today, so worlds replay; the gain is that the next
     per-client static has one place to go. `Bot2_ClearState` calls the registry on respawn and slot release.
   - Check: replay identity, plus a scripted slot reuse (a bot kicked and another added in its slot) in one
     dedicated world.
2. **Dead and stale state** (BM-18, BA-18's dead part). Delete `movement_t.recovery`, `movement_t.commits`,
   `MOVE_SPEND_HOLD`, `Bot2_MoveWaypointIntent`, `Bot2_MoveControl`'s always-true `fast`, the seven unused
   `aim={1,0,0}` locals, `Wants`'s unused argument, the dead store, and `MOVE_DECLINE_OFFMESH`/`MOVE_OFFMESH_YIELD`
   (the decline is held as a no-path decline; keep the event name in logs so tools still read old worlds). Fix the
   stale headers (`bot2_move.h` phase 0, `bot2_defender.h` one defender and the lattice, `ai_bot2.h:38`). The two
   literal 250 u/s walk prices (`PadObserve`, `DropRouteMs`) read `g_speed`: equal at the fixture's 250, so
   lossless there; note it in the commit. Check: replay identity.
3. **Defender settings struct** (BA-18). 94 `bot_*` cvars read by string at 133 sites, none registered.
   - One `defenderSettings_t`, filled once a frame by a table of {name, default, field}, and registered with
     `trap->Cvar_Register` so a mistyped name is visible and `cvarlist bot_defender` lists them.
   - Printed once at the release as one `[DEFENDER] SETTINGS ...` line, which also gives `laps_campaign.py` and the
     identity tool the frozen settings a world ran with.
   - The same table pattern serves the executor's `bot_move*` reads (BM-17: the step function reads cvars that are
     not latched per frame), but that is a second step: the executor reads some cvars inside rollouts.
   - Check: replay identity. Registration can change a cvar's flags (archive), so register with `CVAR_NONE`.
4. **Defender split** (BA-18). Split `bot2_defender.c` (3,373 lines on the branch) by job, behind the existing
   `bot2_defender.h` entry points, keeping `#define defender (*defenderCur)` for now:
   - `bot2_defender_seen.c`: perception and target (`Perceived`, and `Remembered`, which is the same function:
     merge them), hearing, the knowledge rules.
   - `bot2_defender_throw.c`: the throw model (`ThrowVelocity`, the hull sweep, the hurt test). The runner's
     exposure test in `ai_bot2_objective.c:320-366` keeps its own copy with different lethality rules; make the
     shared kernel take the lethality rule as an argument and move both callers onto it in a separate, measured
     commit (it changes the runner's answers where the rules differ).
   - `bot2_defender_shoot.c`: shooting, weapon and loadout choice, the pistol fallback (BA-5 lives here).
   - `bot2_defender_rearm.c`: rearm. The second implementation kept "byte-identical" behind
     `bot_defenderRearmPlan 0` goes only with the owner's word (it is a configuration someone may still use).
   - `bot2_defender_move.c`: role movement (base, chase, the flag return, the keep-out box, `ReachWalk`).
   - `bot2_defender_camper.c`: the camper.
   - Shared small helpers in one place: the own-flag powerup ternary (12 copies), the
     `generic_cmd!=GENCMD_FORCE_SPEED` filter (13 copies in two files), gravity from one source
     (`g_gravity.value`, five sources today), and the frame-length test (`clampTime<=50`) from
     `Bot2_GameStepMsec()`.
   - The 300-line changelog at the top moves into a dated record; the file header keeps the current behaviour.
   - New files go into `codemp/game/CMakeLists.txt`. Check: replay identity on both maps and the standoff world.
5. **Executor functions** (BM-19). Split `Move_Command` (228 lines) into the functions it already contains (knock
   detection, ride ownership, purchase confirmation, chord policing, onward release, replan gating, the step and
   its telemetry), in place, same order. `StraightSearchCore` (464), `StraightJudge` (279) and `WalkStep` (183)
   only after the exec-deep read's open items (BX-2..10) are settled, since they are the code those items change.
   The search scratch shared by all clients stays as it is (safe while searches do not interleave) but gets one
   comment block listing it. Check: replay identity.

## Size and cost

Steps 1-3 are a session each, including identity worlds (two to three 600 s worlds a step, four at once). Step 4
is two sessions (the moves are mechanical, the throw-kernel merge is not). Step 5 is one session. None needs a laps
pair except the throw-kernel merge in step 4.

## Decisions for the owner

- Whether the `bot_defenderRearmPlan 0` path can go.
- Whether the runner's exposure test may move onto the defender's throw kernel (a behaviour change, its own arm).
- Whether registering the defender cvars may change what `cvarlist` shows on a play server (harmless, but
  visible).
