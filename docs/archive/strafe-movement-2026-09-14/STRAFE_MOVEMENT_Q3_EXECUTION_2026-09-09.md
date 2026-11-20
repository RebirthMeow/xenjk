# Strafe movement Q3 execution: integrated quality on one candidate (2026-09-09/10)

Q3 of the post-R9 handoff: the declared 12+12 defended continuity campaign on
the Q2 build, the diagnosis of the candidate's one distant stall, the Q1 repair
it required, and the recollection on the repaired build. Every collection,
including the incomplete and failed ones, is retained under `.codex-research/`.

## Declaration (before collection)

Both arms were frozen with `strafe_continuity_campaign.py freeze` before any
`run`, on the Q2 build `strafe-build-q2-refpath-38e5638bd38c48ac9b04e210a79f5973`
(source manifest matched the checkout at `34049186`, 1104 inputs; reference
cvars empty in every world):

| Arm | Root | Ports | Play arguments |
|---|---|---|---|
| candidate | `.codex-research/strafe-q3-continuity-candidate-20260909-a` | 42000..42011 | `--supported-corners 2 --flight-artifact <r3 payload> --flight-adapter --forecast-guard` |
| baseline | `.codex-research/strafe-q3-continuity-baseline-20260909-a` | 42020..42031 | none |

Cases, rates, seeds and enemies are the R7 set unchanged: red/blue × 1x/5x ×
three repetitions, 600 game seconds, two enemies, pre-actor decision seeds
2026090801..12 shared across arms. The detached chain
(`.codex-research/strafe-q3-continuity-launch.sh`, stage log
`strafe-q3-continuity-20260909-a.chain.log`) ran candidate run/audit, then
baseline run/audit, four worlds at a time.

## Collection `-a` on the Q2 build

**Candidate: all 12 cases valid** (game duration complete, rates and seeds
confirmed, runner tapes distinct within each cell; audit
`strafe-q3-continuity-candidate-20260909-a/aggregate.json`).

| Cell | Runs | Travel s | Travel idle % | Travel walking % | Distant stalls |
|---|---|---|---|---|---|
| red 1x | 3 | 1717.5 | 7.5980 | 30.0661 | 0 |
| red 5x | 3 | 1702.4 | 6.6817 | 36.2767 | 1 (9.0 s) |
| blue 1x | 3 | 1721.0 | 6.8128 | 35.9335 | 0 |
| blue 5x | 3 | 1727.6 | 5.3978 | 34.9845 | 0 |
| pooled | 12 | 6868.5 | 6.6208 | 34.3127 | 1 |

R7's candidate on the R7 build pooled 6.8757 % idle and 33.8856 % walking with
four stalls (24.75 s); these are different builds and not paired trials.

**The stall.** `red-5x-r2`, 480475..489475 ms (9.0 s, 37 samples), anchor
(2524, 5589, 1944), carrying toward the unchanged home objective 7138 units
away, release `stand_entry_resource` throughout. The retained attribution
(`strafe-q3-review-20260909/candidate-stall-attribution.json`, from the R7
`attribute_stalls.py`) samples ownership `route_entry` 1, `supported` 16,
`localnav` 20; a live enemy (client 1, health 45) is within 30.06..32.77 units
in every sample after the first two, standing about 30 units south and
drifting west in step with the runner. The runner's own telemetry issues
forward 0 / right 0 for the whole episode under both `supported` and
`localnav` ownership, with only three recovery steps (`corner_step` at
483100, 484825 and 488875, each 64 units due west, each cut short) and 3..13
units of displacement between them. It is then hit for 55 health at 489725,
walks south into the defender under `supported` and dies at 491475. This is
the pursuit-by-a-body-always-in-front family carried from Q1; body pressure is
not a gate exemption, so the candidate fails the Q3 stall requirement.

**Baseline: incomplete, retained as a failed collection.** Waves one and two
(`red-1x-r1..r3`, `red-5x-r1..r3`, `blue-1x-r1`, `blue-1x-r2`) completed with
clean shutdown. Wave three (`blue-1x-r3`, `blue-5x-r1..r3`) never started:
this session edited `ai_bot2_localnav.c` and `ai_bot2_supported.c` for the
repair below while the baseline arm was still collecting, and
`strafe_play_smoke.py` correctly refused to launch a build whose sources
differ from the checkout (exit 2, `build sources differ from checkout`). The
campaign runner stopped before the next wave and the audit aborted on the
missing directories. This is a process error by the operator of the chain,
not a world outcome; the eight completed cases are audited separately in
`strafe-q3-review-20260909/baseline-a-partial-opposition-audit.json` and are
not spliced into any other collection. They record two baseline distant
stalls of their own (`red-1x-r3` 570900..578150, 7.25 s; `blue-1x-r1`
69725..75475, 5.75 s), consistent with R7's baseline. No candidate/baseline
comparison is claimed on the Q2 build; the comparison script written for it
(`strafe-q3-review-20260909/compare_continuity.py`, a two-arm adaptation of
R7's retained script that reproduces R7's published numbers exactly on the R7
directories) is applied to the `-b` recollection instead.

## Diagnosis

Native prints in defended worlds are untraced, so the mechanism was read from
the retained Q1 pursuing-body traces on the same code paths
(`strafe-q1-shadow-20260909-final5`, `bot_supportedTrace=1`), the Q3
telemetry, and the sources. Three standstills compound while a body occupies
the corridor ahead:

1. **Ground sidestep refused by the wall rule.** `SupportedSidestep` admitted
   only headings whose standing point did not open the waypoint distance by
   more than 8 units, so that a wall is followed rather than oscillated
   against. With a body directly ahead, the player hull diameter (30 units)
   refuses every offset up to 60 degrees and the 90 degree step opens the
   distance past the slack, so nothing is admitted whether the waypoint is 25
   or 120 units away (the new mocked check demonstrates both). The
   `blue-north-s250` trace shows 48 consecutive `walk_refused ... choice=none`
   samples with the waypoint 20..30 units away behind the proxy, and zero
   wish has no bounded life because the walk-stuck exclusion only counts
   commanded frames.
2. **Standing preparation.** `Bot2_SupportedMovement` owns movement at the
   current origin (`prepare_owner_start ... destination=current`) while a
   path is prepared, bounded by the 3 s `preparation_timeout`. When a body
   arrives after acquisition, the runner stands until the timeout, the
   1 s retry, and often re-acquires into the same standstill. The Q3 episode
   spends 4.25 s under `supported` this way; the `red-south-s250` trace shows
   1.3 s standing preparations at the same stand entry.
3. **Recovery steps re-obstructed.** Corner recovery walks 64 units at a
   120 u/s cap after a 1.5 s grace; a body that keeps pace re-enters the
   corridor within 100..150 ms (`recovery_abort` in the traces, the three
   short-lived `corner_step` prints in Q3).

## Repair (Q1 scope: execute recovery under pursuit)

Sources changed: `codemp/game/ai_bot2_localnav.c`, `codemp/game/ai_bot2_supported.c`,
`codemp/game/ai_bot2_README.md`; checks `tools/strafe_localnav_sidestep_check.py`
(new) and `tools/strafe_supported_wait_check.py`.

### v1 (superseded, retained)

Build `.codex-research/strafe-build-q3-detour-a69c0dc461704f0b9923065e52c5ec57`.
The ground corridor reported the refusing entity and a body refusal admitted
a full-reach (48-unit) sidestep opening the waypoint distance by up to that
reach; a standing preparation was released whenever a body was anywhere in
the first 64 units of the raw path. Q1 regressions passed (26/26 temporary
body, 30/30 resources; `strafe-q3-regression-20260910`), but the pursuing-body
fixture (`strafe-q3-shadow-20260910`) showed the design was wrong:
`red-west-s60`, which final5 captured at 30.85 s with no confinement, now
alternated ±90 detours (offset 90, −90, 120, −120 …) while the 60 u/s proxy
re-centred each time, drifted about 150 units away from its waypoint, starved
the recovery scan of the standstill it needs, ended `choice=none` against a
wall, and recorded a 6.75 s confinement with 27.7 % travel idle and no
capture. The preparation release fired on a proxy 53 units ahead
(`prepare_obstructed ... fraction=0.83`) where the old prepared hop path had
simply outrun it. `blue-north-s250` improved (13.25 s → none, 7.6 % idle);
`red-west-s250` and `red-south-s250` gained 5..6.75 s confinements. A v1
traced defended red 5x set (`strafe-q3-traced-defended-20260910`, seeds
2026090804..06, `bot_supportedTrace=1`) recorded one 6.75 s stall in r3 at the
red flag stand with mixed map (`direct_blocked ... body=0`) and body refusals.
The v1 chain was stopped by hand before its Q2 and Q3 stages.

### v2 (the recollection candidate)

Build `.codex-research/strafe-build-q3-detour2-7b3e1a9c4d5f4e6a8b9c0d1e2f3a4b5c`
(1104 inputs, manifest matches the checkout, no warnings in the changed files).

- `GroundCorridor` records the refusing entity (`corridorHit`: the world for
  map geometry, `ENTITYNUM_NONE` when clear, otherwise the entity). The
  refusal branch of `Bot2_MoveLayersNavigation` reads it before the
  alternatives trace again and passes `body` to `SupportedSidestep`; the
  `walk_refused` trace line gains `body=%d`.
- Bounded body detour: on a body refusal the sidestep uses the full 48-unit
  reach and may open the waypoint distance by at most 24 units, at most once
  per second per client (`detourTime`); between opening steps, and for every
  map refusal, the 8-unit wall rule is unchanged. The 750 ms passing-body
  wait, the path-point alternative and the recovery backstop are unchanged.
- `PreparationObstructed`: a standing preparation (not ready, no executing
  prefix, grounded, standing for at least 750 ms) is released with the
  ordinary 1 s retry only when one actual-hull sweep toward the first raw
  point hits a non-world entity within 40 units
  (`release reason=preparation_obstructed`, `[SUPPORTEDTRACE] prepare_obstructed`).

Checks under `.codex-research/strafe-q3-checks-20260909/` (`*-v2`), each
binding input hashes: `strafe_recovery_episode_check.py` pass,
`strafe_localnav_reentry_check.py` pass, `strafe_localnav_sidestep_check.py`
pass (refusing entity reported; near and far waypoint behind a body: wall
rule admits nothing, the detour admits the 60..120 degree step within
before+24 and clear of the body, a second opening step within a second is
refused and admitted again after it; side memory; exclusion; a wall refusal
keeps the wall rule; nothing clear invents no detour), and
`strafe_supported_wait_check.py` pass with two mock seams (`AcquireReference`,
`PreparationObstructed`) and an obstruction-release test. The wait check had
not linked since Q2 introduced `AcquireReference`; the seam repairs that and
its covered contracts are unchanged.

v2 bench (`strafe-q3-repair-v2-bench.sh`, stage log
`strafe-q3-repair-v2-20260910.chain.log`):

| Case | final5 (Q1 build) | v2 |
|---|---|---|
| red-west-static | none / 10.9 % / capture 30.80 s | none / 10.9 % / 30.80 s |
| red-west-s60 | none / 11.7 % / 30.85 s | none / 11.8 % / 30.20 s |
| red-west-s120 | 9.75 s / 48.7 % / none | none / 21.0 % / none |
| red-west-s250 | none / 19.3 % / none | none / 12.6 % / none |
| red-south-s250 | none / 12.6 % / none | none / 10.9 % / none |
| blue-north-static | none / 11.7 % / 29.73 s | none / 11.7 % / 29.85 s |
| blue-north-s120 | none / 12.6 % / 30.23 s | none / 12.6 % / 30.23 s |
| blue-north-s250 | 13.25 s / 45.4 % / none | 5.0 s / 53.8 % / none |

(confinement / travel idle / capture; `strafe-q3-shadow-20260910-v2/shadow-summary.json`).
Twelve `corner_continue` events, eleven followed by goal progress within two
seconds with the proxy within 96 units; 8/8 complete, no death. The static,
s60 and blue s120 worlds reproduce final5 to within a fraction of a second,
so the change is inert where no body obstructs. Q1 regressions on v2:
26/26 temporary body and 30/30 resources (`strafe-q3-regression-20260910-v2`).
The v2 traced defended red 5x set (`strafe-q3-traced-defended-20260910-v2`)
records no distant stall in any of the three worlds (captures 9, 6, 6;
travel idle 5.61 %, 9.75 %, 6.77 %); it is diagnostic evidence, not a gate.

## Recollection chain on the repaired build (v2)

`.codex-research/strafe-q3-repair-v2-chain.sh` (stage log
`strafe-q3-repair-v2-gate-20260910.chain.log`), launched after the bench with
no other world running:

1. Q2 resource regression: 30 candidate and 30 reference worlds with the
   frozen `strafe-q2-selection-20260909-a/selection/payloads`
   (`strafe-q3-resource-regression-v2-launch.py` → `strafe-q3-resource-regression-20260910-v2`);
   gate pair report passed. Result: passed, 30 pairs through every pair
   gate, 60/60 leg measurements valid, no refusal; the reference advantage
   per leg ranges from 275 ms worse to 900 ms better (median 300 ms better),
   the same distribution as Q2's `pair-report-b`. No ratio threshold is
   declared (open plan decision, unchanged).
2. Q3 `-b`: candidate (ports 42400..42411) and baseline (42420..42431) frozen,
   run and audited on the v2 build
   (`strafe-q3-continuity-candidate-20260910-b`, `strafe-q3-continuity-baseline-20260910-b`).

## Collection `-b` on the v2 build

**Candidate: all 12 cases valid** (audit
`strafe-q3-continuity-candidate-20260910-b/aggregate.json`).

| Cell | Runs | Travel s | Travel idle % | Travel walking % | Distant stalls |
|---|---|---|---|---|---|
| red 1x | 3 | 1483.7 | 4.8527 | 34.3146 | 1 (5.75 s) |
| red 5x | 3 | 1719.8 | 6.2507 | 30.3814 | 0 |
| blue 1x | 3 | 1727.1 | 6.8904 | 37.3889 | 1 (8.5 s) |
| blue 5x | 3 | 1736.7 | 7.5577 | 35.2230 | 1 (9.0 s) |
| pooled | 12 | 6667.2 | 6.4457 | 34.3330 | 3 (23.25 s) |

**Three distant stalls, all with a live defender 30..35 units away in every
sample** (`strafe-q3-review-20260909/candidate-b-stall-attribution.json`);
the candidate fails the Q3 stall requirement again.

- `blue-5x-r3` 260875..269875 (9.0 s) and `blue-1x-r3` 396875..405375
  (8.5 s), both at the blue stand entry near (40..58, −1550..−1390, 1912),
  carrying, the defender due north at 30 units. Signature: under `supported`
  ownership the runner commands full forward-right into the body for about
  3 s with zero velocity (a ready path executing into the body until the
  follower's 3 s progress budget refuses it), then `localnav` issues zero
  wish for about 4.5 s while being pushed east a few units per second, then
  the runner is killed. This third standstill (ready-path execution into a
  body) was not visible in the `-a` episode and is not addressed by v2.
- `red-1x-r2` 232875..238625 (5.75 s) mid-map near (1383..1423, 2015..2031,
  1808), carrying at 37 health, the defender 30..35 units south-south-west:
  `localnav` zero wish with two short `recovery` steps, then death.

The v2 detour is not visible in these episodes (no sidestep command in the
sampled telemetry); whether the corridor refusals there were bodies or map
steps (the stand entry has an 18-unit step) is unknown without a trace, so a
traced blue 5x set on the v2 build is the next diagnostic
(`strafe-q3-traced-defended-v2-blue-launch.py`).

**Baseline: all 12 cases valid, no distant stall** (audit
`strafe-q3-continuity-baseline-20260910-b/aggregate.json`). The baseline arm
runs the same v2 build with its ordinary empty play arguments, so the ground
navigation changes apply to it as well; R7's baseline on the R7 build had
four stalls and the eight `-a` baseline cases two.

**Comparison** (`strafe-q3-review-20260909/continuity-comparison.json`,
`passed: false` on `no_candidate_distant_stalls` only):

| Cell | Candidate idle / walking % | Baseline idle / walking % | Candidate stalls | Baseline stalls |
|---|---|---|---|---|
| red 1x | 4.8527 / 34.3146 | 5.3653 / 36.7158 | 1 (5.75 s) | 0 |
| red 5x | 6.2507 / 30.3814 | 7.2615 / 36.7571 | 0 | 0 |
| blue 1x | 6.8904 / 37.3889 | 7.6022 / 37.4176 | 1 (8.5 s) | 0 |
| blue 5x | 7.5577 / 35.2230 | 8.2274 / 39.6559 | 1 (9.0 s) | 0 |
| pooled | 6.4457 / 34.3330 | 7.1178 / 37.6486 | 3 (23.25 s) | 0 |

Idle and walking are not worse in any cell or pooled (they improve in
every cell), seeds map identically, tapes are distinct. The stall
requirement fails. Seeds diversify game-module streams and do not make
these paired trials; the candidate stall count across collections on
different builds is 4 (R7), 1 (`-a`), 3 (`-b`), so a single collection's
count is a weak statistic, but the gate is per collection.

The traced blue 5x set on v2 (`strafe-q3-traced-defended-20260910-v2-blue`,
seeds 2026090810..12) recorded no distant stall in any of its three worlds
(captures 5, 5, 7); the stand-entry refusals it did print are body refusals
(`floor_startsolid sample=1 body=1 choice=wait`) and excluded-heading
sidesteps, never `choice=none`. The `-b` blue stalls therefore remain
untraced; their sampled signature (full wish, zero velocity, `supported`
owner for 3 s) is the ready-path execution budget.

## Repair v3 (execution obstruction release)

Build `.codex-research/strafe-build-q3-detour3-9c4d2b7e1f0a4b3c8d5e6f7a8b9c0d1e`
(1104 inputs, manifest matches the checkout, no warnings in the changed
files). Only `codemp/game/ai_bot2_supported.c` changes on top of v2:

- `BodyAhead` (the v2 sweep, now shared): one actual-hull sweep from the
  origin toward a point, at most 64 units, true when a non-world entity is
  within 40 units along it; `PreparationObstructed` uses it unchanged.
- `ExecutionObstructed`: a ready path whose generated command wants motion
  (`forwardmove` or `rightmove`) while the runner stays under 20 u/s within
  16 units of the same spot for 750 ms, with a body ahead of the follower's
  goal point, is released now (`release reason=execution_obstructed`,
  `[SUPPORTEDTRACE] execute_obstructed`) with the incoming command restored
  and the ordinary 1 s retry, instead of after the follower's 3 s progress
  budget. Applies to the interval-plan follower and the point follower;
  airborne, adapter-owned and moving states are untouched.
- `strafe_supported_wait_check.py` gains the seam (mock returns false; the
  mock follower command has no wish) and passes (`supported-wait-v3`).

v3 bench: `strafe-q3-repair-v3-bench.sh` → `strafe-q3-shadow-20260910-v3`,
`strafe-q3-traced-defended-20260910-v3`, `strafe-q3-regression-20260910-v3`;
gate chain `strafe-q3-repair-v3-chain.sh` → `strafe-q3-resource-regression-20260910-v3`,
`strafe-q3-continuity-{candidate,baseline}-20260910-c` (ports 43100..43131).
First bench attempt (`strafe-q3-shadow-20260910-v3`, launched 01:49 UTC):
invalid, retained. Two of eight worlds completed; six failed at setup with
`Corner intervention refused or unconfirmed` or `Rate command unconfirmed`
(RCON reply `None`), and the traced stage's three worlds never started. At
that time a ComfyUI Python process held 8.1 GB with 1.9 GB of 32 GB free,
the same environment condition the R8 record describes; the failures are
setup timeouts, not behavior, and the chain was stopped by hand. The bench
is to be repeated with `strafe-q3-repair-v3b-bench.sh` (outputs `-v3b`)
once memory is available, then the gate chain.

## v4: pressed-carrier melee (owner decision)

After `-b`, the owner was told that every remaining stall is the same
situation (a no-attack runner pressed at melee range by a defender who keeps
standing in front of it) and that no ground policy escapes it; the owner
chose to let the carrier fight: "saber forward with staff". This is a scope
change to the movement-only runner, declared as a cvar on the candidate arm.

- `bot_carrierMelee` (`CarrierMelee` in `ai_bot2_objective.c`, applied after
  the movement-only button strip): when the runner is grounded with the
  saber, and either has no movement wish or is under 20 u/s while the owner
  wants motion, and a live opposing player (connected, red or blue) is within
  48 units, it faces that player (yaw and pitch), holds forward and presses
  attack. A moving runner keeps its movement command untouched. One
  `[MELEE] swing` print per episode names the target, distance, speed, wish
  and movement owner.
- The runner profile `tools/fixtures/strafe_play/kejim_runner.bot` gains
  `saber1 dual_1` (a staff), so the forward swing is the staff's.
- `strafe_play_smoke.py --carrier-melee` sets the cvar; the Q3 `-c`
  candidate declaration is the v2/v3 play arguments plus `--carrier-melee`,
  the baseline stays plain. The pursuing-body proxy and the M2 temporary body
  are not clients, so the shadow, temporary-body and resource fixtures remain
  pure movement benches on this build.
- `tools/strafe_carrier_melee_check.py` (pass): cvar and weapon/airborne
  gates, a moving runner keeps its command, a blocked wish and a standstill
  both swing, dead/disconnected/same-team/spectator/out-of-range bodies are
  not targets, 48-unit range boundary, aim yaw and pitch, one print per
  episode.
- Telemetry consequence: the smoke summary's `no_attacks` becomes false for
  melee worlds and the opposition audit's runner `alive_attack_samples`
  becomes nonzero; neither is a validity input of the continuity audit.

Build `.codex-research/strafe-build-q3-melee4-2e5f8a1b3c6d4e7f9a0b1c2d3e4f5a6b`
(v3 plus melee). Bench `strafe-q3-repair-v4-bench.sh` (shadow, traced red 5x
with melee on, Q1 regressions) and gate chain `strafe-q3-repair-v4-chain.sh`
(Q2 batches, then `strafe-q3-continuity-{candidate,baseline}-20260910-c`,
ports 43500..43531). Results: pending; ComfyUI was still resident when the
build finished.

## Status and next decision

Q3 is **not passed**. Two complete 12+12 collections exist (`-a` candidate
only, `-b` both arms) and both fail the stall requirement with the same
family: a live defender pressed at about 30 units, the runner standing. On
the v2 build the candidate beats the baseline on idle and walking in every
cell and pooled (6.45 % / 34.33 % versus 7.12 % / 37.65 %) with three stalls
against none. Behavior repairs v1 (withdrawn), v2 (bench-clean, defended
fail) and v3 (built, checks pass, bench invalid for memory) narrowed the
standstills; the owner then chose the direction outside movement scope that
can change the outcome: the carrier fights (v4, `bot_carrierMelee`, staff).

Retained collections and their standing:

| Collection | Build | Standing |
|---|---|---|
| `strafe-q3-continuity-candidate-20260909-a` | Q2 | valid, 1 stall, gate failed |
| `strafe-q3-continuity-baseline-20260909-a` | Q2 | incomplete (8/12), operator error, retained |
| `strafe-q3-shadow-20260910`, `-regression-20260910`, `-traced-defended-20260910` | v1 | v1 withdrawn; regressions passed; fixture regression |
| `strafe-q3-shadow-20260910-v2`, `-regression-20260910-v2`, `-traced-defended-20260910-v2`, `-v2-blue` | v2 | bench clean, no stall in traced worlds |
| `strafe-q3-resource-regression-20260910-v2` | v2 | Q2 pair audit passed, 60/60 legs |
| `strafe-q3-continuity-{candidate,baseline}-20260910-b` | v2 | valid, candidate 3 stalls / baseline 0, gate failed |
| `strafe-q3-shadow-20260910-v3` | v3 | invalid (memory), retained |

Next: run the v4 bench, then the v4 gate chain (Q3 `-c`), on a machine
without ComfyUI resident. Judge `-c` by the same comparison script and the
same gate; classify any remaining stall from its telemetry and the
`[MELEE] swing` prints. If melee removes the pressed-carrier stalls, Q3
closes on the `-c` collection and Q4 starts on that build; if not, the gate
decision (per-collection zero stalls versus a classified body-pressure rule
or pooled collections) goes back to the owner with the `-c` evidence. The
ratio-threshold and measured-failed-timing decisions remain open and
untouched.

