# The tap-admission repair, implemented and measured (2026-09-12 UTC)

Issue 1 of the September 11 handoff: the six branches lost per seeded repetition
to a launch judged as a full flight. The
[diagnosis](STRAFE_MOVEMENT_TAP_LOSS_DIAGNOSIS_2026-09-11.md) named the
mechanism; this record is the repair, its telemetry, its terminal accounting and
three repetitions that measure all of it.

**The named defect is fixed and the repair uncovered a worse-behaved successor
state.** The Speed-clock refusal of a checked tap is gone — 6 / 6 / 6 events to
**1 / 2 / 1**, and **none** of the 30-32 resource refusals per repetition lands
on a low jump — every branch instance now has exactly one terminal record, the
corridor window's worst sample improves by a factor of twenty, and the two
regressions are unchanged. What it exposed is that on the **zero-Force,
no-Speed** cases the branch the runner is no longer ejected from is one it cannot
hold: four cases in every repetition now spend **168-209 frames under
`localnav`**, and the arm's stationary samples go 71 / 73 / 64 to
**436 / 437 / 419**, where step 4 had almost none. Nothing is installed, and this
repair is **not deliverable as it stands**.

## What changed in the runtime

One source file, `codemp/game/ai_bot2_route.c`, hashing
`8a03f087fcb804b915a6d548f5cdf5e99ed80426cc3ef1ae76c6d889002ea0d7`. Compared
with the step-4 source snapshot it is the only file that differs.

### The split the diagnosis asked for

`FastReturnTap` answered two questions with one test and the resource ladder read
the wrong one. It is now two functions:

| Question | Function | Kind |
|---|---|---|
| Does this launch need a flight's resources? | `FastReturnTapChain(route,index)` — the prepared chain — and `FastReturnAuthoredTap(route,group)` — the route's own `up==1` hint | prepared, identical before and after a join |
| May this frame's jump be substituted by a low tap? | `FastReturnOnGuide(state,ps)`, against the published `BOT2_TAP_GUIDE_OFFSET` | this frame only |

`FastReturnTakeoffAdmission` takes `tapChain` and `aligned` separately.
`lowJump` — which gates all three flight reserves — is `tapChain || authored`, and
`aligned` decides only whether the owner writes `state->jumpLow`. The envelope is
**not widened**: outside it the authored jump stands as the route wrote it.

Note what that means for the reserves, because it is easy to get backwards: an
off-guide tap chain still skips all three of them, because **what the launch
costs has not changed** — only whether the owner may substitute a low tap for it.
Letting alignment switch a reserve on would refuse at the commitment what the
join scan and the two-sample lookahead had just admitted, since those callers
have no frame and pass `aligned` false. That is a join releasing on its own first
command frame: the churn of 2026-09-10.

`state->jumpLow` is no longer an input to anything. The authored-tap hint is read
from the route instead, which is what makes the join scan, the two-sample
lookahead and the commitment answer the same question about the same launch —
the property that keeps a join from releasing on its own first command frame.
`FastReturnLatchFresh` exists only so the record can say whether the kernel's
latch agreed; the plan's "reject stale low-jump state" is satisfied by not
reading the latch at all.

`FastReturnGroupClass` now reports the prepared class alone. Its `class=0` used
to mean "this launch is a tap chain" on one frame and "this frame may tap" on the
next, and no reader could tell which.

### Telemetry, fixed before anything was run natively

`[FASTRETURN] admission` now prints the route **the judgement was made on**
(it printed the lane while computing everything from the branch), the branch,
and every fact the ladder decided from:

```
[FASTRETURN] admission client=0 level=2300 route=26 branch=26 index=11 group=0
  takeoff=12 landing=16 dist=355.5 dz=0.0 class=0 tap_chain=1 aligned=1
  guide_offset=9.5 latch=1 latch_fresh=1 authored=1 low_jump=1 speed=425.0
  force=12 speed_remaining=11975 proj_force=12 proj_speed_ms=11975 result=admit
```

The dedupe key carries the route, because a branch and its lane both number
their groups from their own start. `[MOMENTUM]` prints `route=` for the guide the
index belongs to, with `branch=` and `lane=` beside it.

### One entry, one terminal event

`FastReturnBranchClose` is the single exit: it emits `end` on a release, a reset,
a death, an objective or owner change and a disconnect, `abandon` on the two
rejoin-refusal paths, and nothing at all if the instance is already closed. Every
record carries `instance=`, unique per world, `-1` when no branch is running, so
an audit pairs by id rather than by nesting. `Bot2_StrafeRouteReset` splits into
the public entry point, which closes the instance first, and
`StrafeRouteResetState`, which the loading prefix uses for its own follower.

## Evidence

Frozen before the first world: `.codex-research/pkgD-tapfix-20260912/manifest.json`
(2026-09-12T02:46:35Z). Build
`.codex-research/strafe-build-tapfix3-94d7f4640ef74bce82a13f833aecfbf3`. Assets,
cases, flight artifact and world settings byte-identical to step 4, so the
comparison is against the **step-4 candidate arm** and nothing else was
re-collected. Three repetitions of all twenty cases, sixty worlds, every one
exit code 0.

An earlier build, `strafe-build-tapfix2-14294c8f4dd44cd09670e393de98bc41`,
carried three complete repetitions of the same arm before one comment in the
admission ladder was corrected; every figure here was first measured there and
then measured again. Those arms are retained as `tapfix2-candidate-r{1,2,3}`
with `WHY_TAPFIX2_RETAINED.txt`, and they are a second sample of the same
executable.

The step-4 arm's own frozen audits predate the reconciliation and terminal-event
fields, so they are **re-read with today's tools** into this record rather than
quoted; the frozen step-4 outputs are untouched.

### Acceptance, as declared

| | Claim | Step-4 candidate | Tap repair | Verdict |
|---|---|---|---|---|
| A1 | No Speed-clock release of a validated tap | `takeoff_flight_resources` **6 / 6 / 6** | **1 / 2 / 1**, and of the 32 / 32 / 30 resource releases the record can grade, **0 / 0 / 0** have `low_jump=1` | **met** |
| A2 | Unsafe and held launches still refused | body 26/26, resources 30/30; `takeoff_reserve` 7/7/7, receiver 17/20/17 | body **26/26**, resources **30/30**; reserve **8/8/8**, receiver **23/22/21** | **met** |
| A3 | No avoidable churn | 0 / 0 / 0 | **0 / 0 / 0** | **met** |
| A4 | One terminal event per instance | entries with no terminal event **18 / 13 / 15** | **4 / 7 / 3**, every one the world's **last** instance, still running when the capture ended | **met** |
| A5 | The package still works | entries 52/51/52, exits 34/38/37, captured 20/20/20 | entries **46/45/38**, exits **37/34/30**, captured **20/20/20** | **partly — see the regression below** |

The one or two remaining `takeoff_flight_resources` releases are honest and now
say so in one line: lane route 5, group 4, an 827-unit level flight,
`class=20 tap=0`, `tap_chain=0 authored=0 low_jump=0`, `speed_remaining=0`. That
is the rule doing its job. On a pre-repair build no release could be graded this
way at all — `low_jump` did not exist — which is why the step-4 side of A1 rests
on the diagnosis's prepared-group reading and not on this tool.

Per case, the correspondence is exact. In all three step-4 repetitions the loss
was one event in each of R4-red, R4-blue, R5-red, R5-blue, R6b-red, R6b-blue. In
the repair each of those cases ends its branch at the branch's **end** instead:
R4-red `envelope` in all three repetitions, R4-blue `rejoin_refused` in all
three, R6b-red `envelope` in all three, R6b-blue `rejoin_refused` twice and
`envelope` once, R5-blue `rejoin_refused` twice.

The four `takeoff_flight_resources` releases that remain across the three
repetitions are **not** any of those. Every one of them is the same lane launch —
route 5 red or route 4 blue, `branch=-1`, group 4, takeoff 43 to landing 50,
`class=20 tap_chain=0 authored=0 low_jump=0`, `speed_remaining=0`, 675-680 units
of level flight with no Speed window. R5-red hits it in all three repetitions and
R5-blue in one. That is the rule refusing a flight, which is its job.

### What the six recovered launches bought, and what they did not

They did **not** become six more admitted rejoins. Exits are 34/38/37 to
37/34/30 — inside the control spread, called inconclusive by the report tool.
What happens instead is that the runner now flies the branch to its end and the
**rejoin there** is refused: `abandon` records go 0 / 0 / 0 to 3 / 2 / 2 and
releases 30/33/30 to 39/38/37. The misnamed refusal became a correctly named one,
a few samples of route later.

Branch entries fall 52/51/52 to 46/45/38 because a mid-branch release used to be
followed by a re-entry; the report calls that gap reportable.

### The regression this exposed

| Measure | Step-4 candidate | Tap repair | Verdict |
|---|---|---|---|
| `localnav` frames | 2 / 9 / 2 | **775 / 765 / 735** | reportable |
| Stationary samples | 71 / 73 / 64 | **436 / 437 / 419** | reportable |
| Zero-input samples | 114 / 128 / 111 | **656 / 627 / 619** | reportable |
| Longest grounded slow window | 2325 / 2325 / 2325 ms | **3350 / 3350 / 5475 ms** | reportable |
| Route-entry frames | 40 / 41 / 42 | 33 / 34 / 26 | reportable |

It is not spread across the arm, and it is close to deterministic. In every
repetition it is the same four cases, with `localnav` frames repeating almost
exactly: **R4-red 168 / 168 / 168, R4-blue 202 / 202 / 202, R6b-red
192 / 192 / 192, R6b-blue 209 / 200 / 173**. They are exactly the **zero-Force,
no-Speed** families. R5, which has a Speed window for its first three seconds,
records **0 / 0 / 0** `localnav` frames in both directions: after its own
rejoin refusal at branch index 43 the supported executor takes over for 321
consecutive samples with zero stationary ones, and its height never changes.

**The mechanism is a fall off the corridor, and it is measured, not inferred.**
In all four cases the runner leaves the floor around branch index 34-38 and free
falls about four hundred units:

| Case | branch index at release | release | vz at release | z before → after |
|---|---|---|---|---|
| R4-red | 34 | `envelope` | −517.7 | 1808 → **1368** (drop 440) |
| R4-blue | 36 | `rejoin_refused` (abandon reason `envelope`) | −677.7 | 1740 → **1368** (drop 372) |
| R6b-red | 34 | `envelope` | −517.7 | 1856 → **1368** (drop 488) |
| R6b-blue | 38 | `rejoin_refused` (abandon reason `envelope`) | −677.7 | 1774 → **1368** (drop 406) |
| R5-blue | 43 | `rejoin_refused` | — | 1808 → 1808, no descent |

Heights are repetition 1. The index and reason repeat in all three repetitions
except R6b-blue, whose release walks 38 / 37 / 34 and whose reason is
`rejoin_refused` twice and `envelope` once — which is the same event either way.
R5-red never ends a branch this way, and R5-blue does in two of three.

The 40 Hz trace shows the fall starting well before the release: R4-red is
already airborne at level 8450 with `vz=-159` and the lane keeps steering it
through eighteen more frames of accelerating descent before the 256-unit envelope
finally trips at `vz=-517`. `envelope` and `rejoin_refused` are not two different
events here — they are which gate caught the same fall, the exit gate when the
runner had reached the branch's exit zone and the lane envelope when it had not.

**Nobody dies and nothing is lost.** Falls 0, deaths 0 and captures 20/20 in all
three repetitions, with a worst health floor of 90 across the arm: z 1368 is a
lower floor, not the chasm. The whole cost is the three to five seconds of
`localnav` spent climbing back.

An earlier reading of this — that BR-A is authored at the 425 u/s Force-Speed cap
and a 250 u/s runner simply cannot hold the line — is **not** what the traces
say, and is withdrawn. The runner is falling, not lagging. What the evidence does
not yet settle is *why* it leaves the floor there: whether the branch line runs
off an edge that is only safe at the speed it was authored from, or whether the
runner is already off the line by index 34. `tapSafe` checks the authored line's
floor, not the trajectory a slower runner actually flies, so it would not catch
either. That is the next session's question, and this is the first time the
branch has been flown that far to ask it.

### What it costs, and what it does not

Every world captured in every repetition of both arms. Over the whole case set
the pickup-to-capture median is **17.73 s to 17.77 s** (116 against 110 completed
episodes), so the stalls are not moving the arm's central time. What they cost is
returns per world: six fewer complete pickup-to-capture cycles fit in the same
fifty-five seconds.

### Declared windows

| Window | Step-4 candidate (met / scored, median, worst) | Tap repair |
|---|---|---|
| corridor | 84/93, 92/98, 87/96 — worst **12.6 / 11.3 / 7.9** u/s | 89/89, 85/86, 73/74 — worst **231.4 / 227.0 / 231.4** u/s |
| entry | 34/34, 32/33, 34/34 — worst 302.0 / 298.7 / 299.2 | 26/28, 25/27, 18/20 — worst **141.0 / 136.8 / 141.0** |
| descent | 50/52, 53/55, 50/52 — median 361.4 | 48/51, 47/49, 41/43 — median 361.4-361.9 |
| blue-choke | 44/44, 42/42, 44/44 — median 425.0 | 37/37, 37/37, 30/30 — median 425.0 |
| T2 landing turn | 0 of 8 / 12 / 8 | 0 of 13 / 12 / 13 |

The corridor is the window BR-A actually runs, and the step-4 record's own
complaint about it — "the corridor's *worst* single sample gets worse" — is
answered: 7.9-12.6 u/s to 227-231, and every scored visit meets its floor in two
of three repetitions. The entry window is worse on its worst visit and scores
fewer visits, which follows from the same change: fewer entries, and the ones
that remain are entered from slower states.

## Pure checks and suites

- `tools/strafe_fast_return_check.py` — the split, the envelope taken from the
  published constant, the stale latch, the full commitment path at 26 Force with
  675 ms of Speed on and off the guide, and one terminal record per instance on
  exit, abandon, release, reset and disconnect. Its new assertions were
  **falsified against the pre-repair ladder**: rebuilt with the old rule the
  check fails on the off-guide tap-chain case, which is the shape of the six
  retained events.
- `tools/strafe_catalog_check.py`, `tools/strafe_route_resume_check.py` — pass on
  the changed source.
- Python suite from the repository root: **459 tests, OK**, including four new
  cases in `tools/test_strafe_branch_report.py` for the `end` event, instance
  pairing, records without ids, and the completion denominator.
- `tools/strafe_momentum_audit.py` learns `end` as a terminal event and pairs by
  `instance`; it still closes an un-terminated span by substitution, so pre-repair
  arms stay readable.

## Open items this leaves

1. **The fall off the corridor at branch index 34-38.** On the zero-Force,
   no-Speed cases BR-A drops the runner about four hundred units to z 1368, the
   lane releases mid-fall, and the recovery costs three to five seconds under
   `localnav`. Survivable and still capturing, but it is the branch putting the
   runner off a ledge on the very family it exists for. Find out first whether
   the authored line leaves the floor there or the runner leaves the line, then
   decide between fixing the line, gating the entry on what the runner can fly,
   and letting the executor close on the guide instead of releasing. Evidence:
   `.codex-research/pkgD-tapfix-20260912/candidate-r{1,2,3}/R{4,6b}-{red,blue}`,
   whose play CSVs carry the height and whose `[MOMENTUM]` traces carry the
   descent.
2. **The rejoin at the branch's end** now refuses 2-5 times per repetition where
   the tap loss used to hide it. BR-A's rejoin row was already open; it is now
   the dominant branch outcome for these cases and can be measured natively,
   which the BR-A record said was still required.
3. **A censored end is not an unreconciled one.** `strafe_branch_report.py` still
   blocks every branch-frame comparison while any entry lacks a terminal event,
   and the 17 remaining ones are all worlds that stopped mid-branch. The runtime
   can now tell those apart; the tool cannot. That is a measurement-contract
   change and is declared, not slipped in.
4. **T2, T4 and T5 are untouched** by this repair and were not scored here.

## Three collection errors, two of them retained

**One.** During the first attempt at repetition 3, one over-long comment line in
`ai_bot2_route.c` was rewrapped while worlds were still launching. That moved the
file's hash off the one the build recorded and `strafe_play_smoke.py` refused
sixteen of the twenty worlds with `build sources differ from checkout`. The check
did exactly its job. The edit was reverted byte-for-byte and repetition 3 was
collected again. Retained at
`.codex-research/pkgD-tapfix-20260912/candidate-r3-sourcedrift/` with its own
`WHY_RETAINED.txt`; its four good worlds are not part of any scored arm.

**Two.** After all three repetitions were collected on
`strafe-build-tapfix2-…`, a comment in `FastReturnTakeoffAdmission` was found to
describe behaviour the code does not have — it said the three reserves judge an
off-guide tap chain, and they deliberately do not. Correcting it changes the
file's hash, which would have left the scored native evidence bound to a build
whose sources no longer match the checkout: the invariant every handoff tells the
next session to verify. So the comment was corrected, the build remade as
`strafe-build-tapfix3-…`, and all three repetitions and both regression suites
were collected again. The superseded arms are retained as
`tapfix2-candidate-r{1,2,3}` with `WHY_TAPFIX2_RETAINED.txt`. The executable
difference between the two builds is one comment; treat them as two samples.

**Three, not retained.** The tapfix2 regression outputs were deleted rather than
renamed before the tapfix3 suites were run, so `.codex-research/pkgD-tapfix-regression`
now holds only the tapfix3 run. Both runs gave the same result (26/26 body,
30/30 resources), and that is recorded here, but the tapfix2 bytes are gone. That
is a break with the project's retain-everything convention and it is recorded as
one rather than passed over.

## Materials

| What | Path |
|---|---|
| Frozen declaration, runner, scorers | `.codex-research/pkgD-tapfix-20260912/{manifest.json,freeze_manifest.py,run_cases.py,collect.sh,collect-r3.sh,score.sh,regressions.sh,tap_losses.py,summarize.py,verify_hashes.py,check_links.py}` |
| Three repetitions, 60 worlds | `.codex-research/pkgD-tapfix-20260912/candidate-r{1,2,3}/` |
| Audits, losses, windows, falls | `.codex-research/pkgD-tapfix-20260912/candidate-r{1,2,3}-{audit,losses,windows,falls}.json` |
| Step-4 arm re-read with today's tools | `.codex-research/pkgD-tapfix-20260912/step4-candidate-r{1,2,3}-{audit,losses}.json` |
| Scored comparison | `.codex-research/pkgD-tapfix-20260912/report-tapfix.{json,log}`; every table in this record reproduces from `summarize.py` |
| Superseded arms on the pre-comment build, retained | `.codex-research/pkgD-tapfix-20260912/tapfix2-candidate-r{1,2,3}/` + `WHY_TAPFIX2_RETAINED.txt` |
| Retained failed repetition | `.codex-research/pkgD-tapfix-20260912/candidate-r3-sourcedrift/` + `WHY_RETAINED.txt` |
| Preflight recipe world (first build, retained) | `.codex-research/pkgD-tapfix-20260912/preflight` |
| Regressions | `.codex-research/pkgD-tapfix-regression/` (26/26 body, 30/30 resources) |
| Pure checks | `.codex-research/pkgD-tap-checks/{fast-return-v12,catalog-v5,route-resume-v4}`, all three bound to the scored hash by `pkgD-tapfix-20260912/verify_hashes.py`; falsification at `.codex-research/pkgD-tap-checks/falsify-old-rule` |
| Builds | `strafe-build-tapfix3-94d7f4640ef74bce82a13f833aecfbf3` (scored), `strafe-build-tapfix2-14294c8f4dd44cd09670e393de98bc41` (superseded arms), `strafe-build-tapfix-707c807912164e4094ff121ce124400b` (preflight only) |
