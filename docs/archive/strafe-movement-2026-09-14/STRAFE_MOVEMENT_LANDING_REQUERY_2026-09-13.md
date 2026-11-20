# Landing requery and normal-speed defended discovery

This is the **first trial**, superseded by [background preparation](STRAFE_MOVEMENT_BACKGROUND_RECOVERY_2026-09-13.md). Its immediate
prefix execution caused extra braking, so it is not the final implementation.
The defended discovery below belongs to this frozen trial build.

## Scope and implementation

Main-task work, with no subagents or installed-game changes. This cycle follows
the [join approach repair](STRAFE_MOVEMENT_JOIN_APPROACH_2026-09-13.md) and keeps
the acceptance contract unchanged.

The six recurring seeded descent misses were ordinary vertices with no turn
speed limit, 100–150 ms after the last airborne sample. Their ground prefix was
queried before the descent. The corridor refusal was valid; the old unconditional
one-second query backoff prevented supported traversal from considering the
actual landing state while local navigation recovered.

`ai_bot2_supported.c` now permits a query on the next 25 ms frame after this
specific preparation refusal. Eligibility requires world ground, an airborne
sample in the preceding 250 ms, at least 64 units from the query root, a lateral
`crossing_not_passed` refusal with no positive vertex speed limit, and no early
requery in the preceding second. The old prefix is released and the incoming
command restored. The next query starts at the actual player origin and passes
through ordinary native preparation. Another failure retains ordinary backoff.

This is general controller logic: no route name, map name, rescue coordinate or
resource grant. It neither widens the corridor nor waives crossing speed. It
repairs query latency after a miss, not the stale descent prefix itself.

## Solo results

Baseline: `.codex-research/takeover-a3fa9a86cffa4f16b928d6bb3af25c63`.
Candidate: `.codex-research/landing-requery-b64a15afc09445b6b2cbfbb678c67896`.
Build: `.codex-research/strafe-build-landing-f8ed5f304a5a4ea88885af99bee31542`.
The same twenty declared cases, three repetitions, assets, settings and audits
are retained. These are case comparisons, not deterministic paired trajectories.

- 60/60 worlds captured; no deaths, falls or lower-floor departures; health floor
  90; churn zero. All 18 eligible releases queried again after exactly 25 ms.
- The six affected first returns used 25 ms of localnav each, compared with
  roughly 100–150 ms previously. The lateral refusals still occur and are counted.
- Mean of the twenty case capture-time deltas: **+0.021 s**. Eight case means are
  faster; 30/60 individual time pairs are slower. R4-red mean is +0.117 s and
  R4-blue +0.067 s. No general speed improvement is established.
- First-return idle time totals 600 ms in each arm; walking totals 223,875 ms
  before and 224,825 ms after. Four pairs have worse idle fraction and 27 worse
  walking fraction. Both arms have zero uncovered first-return trace time.
- T5 remains **0/8** in every repetition. All six R5 approaches still admit;
  none abort. Full-movement qualification remains open.

Full-world counts include later laps and differing end-of-world coverage; they
must not be presented as equal-duration first-return rates:

| Measure | Baseline r1/r2/r3 | Candidate r1/r2/r3 |
|---|---|---|
| Localnav frames | 35 / 82 / 49 | 15 / 60 / 56 |
| Stationary frames, speed <20 | 60 / 83 / 44 | 47 / 96 / 86 |
| Zero-input frames | 208 / 240 / 204 | 188 / 224 / 191 |
| Contract releases, including landing requery | 6 / 7 / 6 | 6 / 7 / 7 |
| Branch entries without terminal event at end | 7 / 2 / 1 | 3 / 2 / 2 |

The extra later-lap R5-blue speed refusal occurs in candidate r2 and r3, versus
baseline r2. Its recorded state is identical: level 49400, speed 435.847,
vertex limit 390.957 plus the unchanged 40-unit allowance, height -0.125 and
lateral 0.374. It retains the full 1000 ms retry and re-acquires at level 50400.
The landing exception does not apply. This is a valid overspeed refusal whose
upstream arrival and recovery remain a performance target, not a gate to waive.

## Verification

The extracted supported-dispatch check passes, including command restoration,
fresh query origin, cooldown and ineligible speed/turn/old-landing/near-root
cases. A retained counterfactual removes only the production helper call and
fails the dispatch assertion on the old retry delay. Corner-span, seam,
path-release and fast-return checks also pass.

Native affected regressions: 26/26 temporary-body cases pass their subset
thresholds; 30/30 resource cases complete, with no refusals and unchanged source
bindings. Neither subset establishes the broader milestone. The initial mock
compile failure from a missing world-entity constant is retained; the corrected
check is `supported-check2`.

`verification-solo.json` matches all 1104 measured sources and both binaries.
The native build and collection were frozen. Sources before this cycle and
exact cycle diffs are retained under `before/` and the evidence root's patches.

## Defended discovery

Two declared 600-second normal-speed worlds, red and blue, use two opponents
(CHASE and BASE), pre-actor seeds 2026090801 and 2026090807, native 25 ms game
steps, and the candidate's unchanged assets and movement settings. Commands,
source snapshot, hashes and all recordings are under `defended/`. This is a
two-world discovery set, not the twelve-run qualification campaign or a paired
combat improvement claim. Both worlds completed with confirmed seeds, roles and
rates. Source and binary bindings still match in `verification-final.json`.

| Within declared 600 s | Red | Blue |
|---|---:|---:|
| Captures | 13 | 9 |
| Deaths | 7 | 11 |
| Grounded idle seconds | 20.25 | 16.25 |
| Grounded walking seconds | 202.85 | 195.10 |
| Alive observed seconds | 592.45 | 586.675 |
| Longest grounded idle | 2.25 s | 2.00 s |
| Confinement episodes | 0 | 0 |
| Plunges | 0 | 1 |

There were no landing-requery events in either defended world. These observations
therefore do not demonstrate the repair under combat and cannot qualify a later
build. Grounded walking was mostly supported outbound travel without Speed:
153.525 s red and 142.45 s blue. The longest red walking interval lasted 9.25 s
with Force 100 throughout. Resource shortage alone does not explain the walking.
`walking-attribution.json` retains all episodes and owner/resource-state totals.

Blue's actual plunge is an unresolved safety failure, ending in the `8:50`
`MOD_FALLING` self-kill. The old chasm audit associates kills with the end of the
whole below-floor episode and leaves this episode's kill null; the exact game
log confirms it. The plunge count itself is unchanged.

The detailed sequence is in `departure-attribution.json`, the retained 40 Hz
momentum log and `blue-departure-rows.json`:

1. Flight adapter window 11, candidate 2, prepared at level 525425 and entered
   at 525450 after Speed expired. The CSV's broader `supported` owner label hides
   this adapter; it is not evidence that the pure ground follower chose the hop.
2. The adapter issued a jump at 525700. Vertical velocity was 301.5 at 525725,
   282.3 at 525750, then -6.7 at 525775. Health remained 95 across the launch.
3. An opposing bot was near and above the runner: its nearest retained sample
   at 525800 was (1935.425, 2768.381, 1888.676), versus the runner at 525725 at
   (1919.520, 2747.278, 1815.451). A body collision is plausible, not proved by
   these undersampled, non-simultaneous positions.
4. Flight refused at 525875, failed recovery at 525975, then the supported path
   refused a lateral miss of 97.440 units and localnav took over. The runner
   reached the bottom and died. Calling this ordinary knockback or a confirmed
   self-steered map-edge departure would overstate the evidence.

The next safety experiment needs the native collision entity and forecast/live
contact divergence. Speed expiry coincided with entry but is not established as
the cause: `BG_AdjustClientSpeed` resets speed from basespeed inside each Pmove,
so an assumed stale cached speed is not sufficient grounds for a gate change.
Do not fix this by suppressing one map window or by editing T6's falling clause.

## Evidence and next decision

Under the candidate evidence root: `comparison.json`, `totals-v2.json`,
`report.json`, `movement-diagnostics.json`, `landing-events.json`, their retained
drivers, `candidate-r{1,2,3}`, `temporary-body`, `resources`, and `defended`.

Keep the recovery-latency claim narrow. A future performance repair should
address the actual descent prefix or its braking after reacquisition, using
R4 and the later-lap R5-blue overspeed case as checks. Do not treat fewer localnav
frames or a renamed release as proof of less walking. Defended departure and
confinement evidence determines the next safety work; T5, T6 and K07 stay open.
