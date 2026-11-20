# Kejim playable candidate checkpoint

The owner requested immediate in-game play on September 9 local / September 10
UTC, confirmed ComfyUI was closed, and asked to avoid another open-ended block
investigation. This checkpoint delivers a provisional human playtest separately
from full K07 qualification. Existing failed gates remain retained and open.

## Direction and bounded work

Keep native staff melee against a nearby live enemy when the runner is actually
stopped. Preserve flight and jump ownership, retain normal damage and respawns,
and use the existing floor/hazard check before a forward melee step. A saber kill
can legitimately clear an opponent; an enemy self-kill is not evidence of this.
Do not start another navigation redesign without a demonstrated delivery defect.

The bounded delivery sequence is: finish the frozen v4 bench; repair the reviewed
command-ownership defects; make one fresh build; run focused resource/body,
real-opponent, normal MP listen and isolated movement-cost checks; install with
recoverable archives and explicit playtest configuration. The 12+12 Q3 campaign,
full M2 and remaining final qualification are subsequent qualification work,
not implied passes from the playtest.

## Frozen v4 evidence

Build: `.codex-research/strafe-build-q3-melee4-2e5f8a1b3c6d4e7f9a0b1c2d3e4f5a6b`.
All 1104 manifest source hashes matched before collection. No ComfyUI/native
world was resident and 18.44 GiB physical memory was available. The existing
bench wrapper ran under Git Bash; its `date` executable was absent from PATH,
so wrapper stage timestamps are blank. Native server timestamps and per-run
manifests remain present; this does not supply timing qualification.

- `strafe-q3-shadow-20260910-v4`: eight completed proxy cases, four native
  continuations with subsequent goal progress, one confinement case. The proxy
  cannot take saber damage and does not measure melee efficacy.
- `strafe-q3-traced-defended-20260910-v4`: three red 5x, 600-game-second worlds
  completed with 8/7/6 captures, 10/16/17 deaths and no audited distant stall.
  Native `MOD_SABER` kills by Runner_v2 occur in all three. These changed combat
  timelines establish actual attack outcomes, not a controlled causal cure or
  full Q3 qualification. Travel idle is 4.44/5.88/4.92 percent; walking is
  39.85/37.83/36.71 percent. One near-goal confinement remains classified by
  the existing auditor as arrival/wait.
- The same trace catches a swing at 156.9 u/s with no movement wish. Review
  also found that grounded flight/jump commands can be overwritten, and a point
  follower obstruction refusal fails to restore its incoming command. These
  motivate narrow corrections before installation.

## Corrected candidate

Build: `.codex-research/strafe-build-playtest-20260910-ownership`.
The production changes are deliberately limited to command ownership:

- Melee requires horizontal speed below 20 u/s, no jump/crouch input, and no
  ready/active/recovering flight state. The existing native `TraceFloorScore`
  gates forward movement; a refused step still permits a stationary swing.
  This reuses the current floor policy, not a new collision/visibility proof.
- Supported interval obstruction cannot release a command owned by a flight
  or connection adapter. The point follower restores its incoming command on
  execution-obstruction refusal, as the interval follower already does.
- Extracted production checks pass in `strafe-playtest-melee-check-20260910`
  and `strafe-playtest-supported-check-20260910`, including sentinel command
  preservation, all flight states, fast coasting, jump input and floor refusal.

Bounded native collection: `.codex-research/strafe-playtest-20260910/plan.json`.
Temporary-body regressions pass 26/26. Two 600-game-second matches at 5x with
two real opponents complete on both teams: red 7 captures/12 deaths, blue
6 captures/10 deaths, no audited distant confinement. Native saber kills are
1 red and 3 blue. Travel idle is 5.08/5.67 percent and walking 42.21/40.97
percent. Blue retains a near-goal wait. These are playable-development outcomes,
not the 12+12 baseline comparison or a claim that walking passes Q3.

Resources: 30/30 pass the existing report. Normal MP listen runs with one
opponent per team complete 120 game seconds, red with two captures and blue
with one; 11/16 sampled jump inputs, expected loadout and roles, route use,
flight artifact loading and clean shutdowns. All four play worlds bind the
new build. The full 30 reference pairs are not recollected here.

## Isolated cost and remaining limits

`.codex-research/strafe-playtest-cost-20260910/` retains the six sequential
R8-declared workloads on the corrected build. Outcome worlds and compilation
had finished; idle MSBuild worker processes remained resident. No parallel
agent, test or source scan ran during measurement.

| Sample | Movement p95 / max ms | Server p95 / max ms | Standing |
|---|---|---|---|
| Eight-bot 1 | 0.3007 / 5.2663 | 1.1782 / 6.4319 | Maximum fails |
| Eight-bot 2 | 0.2892 / 2.8248 | 0.9658 / 3.1253 | Cost gates pass |
| Eight-bot 3 | 0.3225 / 5.3348 | 1.3137 / 5.6712 | Maximum fails |
| Eight-bot 4 | 0.3124 / 4.6884 | 1.0178 / 5.1791 | Cost gates pass |
| Control | 0.2927 / 0.5358 | 1.0047 / 1.8687 | Cost gates pass |
| Sixteen-bot stress | 0.5651 / 3.3312 | 2.0598 / 4.2307 | Invalid sustained activity: 106 dead-client frames, 105 missing-dispatch frames |

All four eight-bot samples and the control have zero dead-client or missing
dispatch frames. Preserve all samples, including stress failure. This fixture
times `Bot2_ExecuteMovement` and Pmove-boundary preparation; it excludes
objective selection and `CarrierMelee`. It is not full playable-combat cost.
K07 and Q4 remain open. Do not declare the passing samples a cohort pass.

## Installed provisional candidate and next action

The archive-safe installer staged the corrected DLL/symbols in GameData and
base, the catalog, staff runner profiles, `runner_flight.txt` and
`runner_playtest.cfg`. Archive:
`.codex-archive/20260910T0248073047206Z/manifest.json`. Every installed file is
hash-verified. No deletion, engine replacement or push was performed.

Use normal MP Create Game / CTF / Kejim, then `exec runner_playtest.cfg` and
`addrunner`; optional `addbot2enemy 1` and `botplayrate 5`. The
[play guide](KEJIM_RUNNER_PLAY.md) includes restore instructions. Human play
feedback is now the next useful input, not another automatic discovery sweep.
Remaining qualification: full Q3 comparison, full disturbance retention,
reference-ratio policy, and timing repair followed by final integrated checks.
