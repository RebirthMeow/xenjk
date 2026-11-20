# Fast return lanes: bounded playable implementation

The owner found the pickup/pad repair insufficient: running the flag home
conservatively is too exposed. This package makes fast lane travel available
during the return and uses supported navigation to connect/recover between
admitted lane sections. Full K07 qualification remains open.

## Implementation

Opt-in `bot_fastReturn 1`, integrated before ground recovery/supported
navigation for the movement-only runner. `ai_bot2_route.c` owns separate
per-client state and reuses the catalog's stand-to-stand geometry (entries
4/5 for this catalog), without changing legacy whole-guide resource gates.

- Select the corresponding return lane within 1800 units of the enemy stand
  during approach and prepare its geometry within the existing shared
  32-trace/frame budget.
- At actual grounded pickup/recovery state, join a nearby supported lane
  point within 128 units, with hull clearance, four floor probes, and the
  existing momentum/run-up refusal. No teleport or reset of velocity/resources.
- Follow the lane with the existing feedback steering. Recheck each takeoff
  against actual momentum; a sustained jump needs the native jump reserve.
- Ordinary tap variants require a prechecked, level floor corridor sampled
  every 32 units, an inflated hull with 100 units of overhead jump clearance,
  and no more than 24 units of lateral error at admission. A level/downhill
  receiver alone does not establish a safe tap.
- Speed expiry alone does not eject this separate owner. It can request native
  Speed on the ground at 60 Force, leaving a jump reserve after activation;
  it never waits to recharge. Native gameplay owns every resource change.
- Preserve active prepared-flight ownership, combat inputs, native physics
  gates, clock continuity and two-frame missed-jump handback. Refusals preserve
  incoming commands and allow a later grounded join. Pad steering retains its
  existing higher priority.

This is empirical known-map lane execution, not an actual-state forecast proof
for arbitrary trajectories. Airborne pickup does not yet initiate a new lane
join. Severely depleted first returns can still spend substantial time in
supported recovery. Local floor/clearance checks do not certify all enemy
interventions or every arrival velocity.

## Retained development evidence

| Build | Evidence | Outcome |
|---|---|---|
| `strafe-build-fastreturn-20260910-a` | `fastreturn-20260910-a` | Four 26-Force cases, both teams with no Speed or 8-second Speed: three captures each, no deaths. Review found unsafe low-Force tap substitution and missing missed-jump guard; not delivered. |
| `strafe-build-fastreturn-20260910-b` | `fastreturn-20260910-b` | Blanket refusal of under-resourced sustained jumps caused two falling deaths in the Speed cases after poor mid-lane recovery. Preserve failures; not delivered. |
| `strafe-build-fastreturn-20260910-c` | `fastreturn-20260910-c` | Adds checked tap corridors, narrower joins and local Speed activation. Eight solo cases: natural red/blue plus both teams at 26 Force with 8-second/no Speed and zero Force/no Speed. 31 captures, zero deaths. |
| `strafe-build-fastreturn-20260910-d` | `fastreturn-playtest-20260910-d` | Final source adds explicit attack ownership and Jump/Speed level-3 gates; normal MP defended and pad checks below. |

All paths in that table are under `.codex-research/`. Each runner retains
its declaration, source/binary bindings, logs and CSV/demo. These builds are
distinct; development evidence is not relabeled as final-build qualification.

## Return measurements

`tools/strafe_return_audit.py` retains captures, deaths, flag loss and censored
episodes; displacement from pickup is explicitly not certified route progress.
Four synthetic episode tests pass. Speed and hopping evidence are sampled;
4 Hz logs cannot count every jump or certify between-sample speed.

The prior installed candidate's natural return median was 24.75 seconds
(eight captures, one censored). Candidate-c's two natural worlds produce seven
captures in seven episodes, median 11.825 seconds and median per-episode
sampled speed about 565 u/s. Per-team capture medians: red 13.1625 seconds
(previous 25.10), blue 11.80 (previous 24.4375).
Reports: `fastreturn-baseline-20260910/natural.json` and
`fastreturn-20260910-c/natural-comparison.json`.

Do not hide the weak case: the first natural red return at 16 Force took
22.40 seconds. Its first 512-unit displacement took 3.25 seconds, versus
3.0 seconds in the prior 12-Force example; 1024 units took 4.0 versus 5.25.
The states are unmatched. This is faster later travel, not universal improvement
in initial pickup clearance. Corner first returns ranged 15.7–22.35 seconds.

Final-d normal MP, one opponent, 120 seconds at normal speed on each team:
red two captures/one death, blue one capture/three deaths. All runner deaths
are native weapon kills, not falling. Eight pickup episodes retain three
captures, three carrier deaths and two ending censored returns. Captured
returns take 23.825, 14.325 and 13.075 seconds (median 14.325); prior defended
capture median was 26.35 seconds. These are small unmatched cohorts, not a
statistically established survival improvement.

No audited confinement/distant stalls. Overall idle 2.5% red/6.2% blue,
walking 41.2%/23.1%; longest sampled idle 1.5/2.75 seconds. These include
outbound travel and are not exclusively return metrics.
See `fastreturn-playtest-20260910-d/return-audit.json`,
`opposition-audit.json` and each world's `base/games.log`.

Both final-d pad cases with fast return enabled land alive and shut down cleanly.
The existing 26 temporary-body cases pass on final-d with fast return off:
`fastreturn-body-20260910-d/summary.json`. This preserves the legacy route
regression, not a 26-case fast-return body qualification.
The unchanged 30-case resource report also passes with fast return off:
`fastreturn-legacy-resources-20260910-d-report/summary.json`. Fast-return
resource evidence is the separately declared eight-case development set and
final normal MP worlds, not a relabeling of the old no-Force-spending gate.

## Focused checks and remaining scope

`tools/strafe_fast_return_check.py` extracts actual dispatch and tap-admission
logic; final-d passes at `fastreturn-check-20260910-d/result.json`. Covers
command/PS preservation, preload-only approach, joins, team/objective/flight
ownership, momentum/reserve gates, tap corridor flags, clock/missed-jump
handback and the Speed 59/60 boundary. The floor producer and native motion
are mocked there; native outcomes above are separate.

K07, full Q3/M2 retention, timing and broad map transfer remain unqualified.
No new timing claim is made. The next useful input after installation is
human pressure on pickup and return, with particular attention to the first
low-Force return and whether recovery promptly rejoins fast travel.

## Installed version and resume instruction

Final-d is installed with `bot_fastReturn 1` in `runner_playtest.cfg`; all
nine installed hashes verify. Previous installation archived at
`.codex-archive/20260910T0426153079480Z/manifest.json`. To restore, close the
game, archive the replacement files, and move each saved entry to the
manifest's original absolute path. No deletion, engine replacement or push.

Use a fresh normal CTF Kejim game, `exec runner_playtest.cfg`, then `addrunner`.
CSV/demo capture remains enabled. For an in-session diagnostic comparison,
`set bot_fastReturn 0` selects the prior conservative controller; `1` enables
the new lane owner again. This does not restore the old binary. Do not rerun
the historical full K07 chain before reading new human feedback.
