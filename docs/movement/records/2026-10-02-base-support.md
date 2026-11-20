# Milestone B3: base support in the 4v4 (2026-10-02)

Plan: [MILESTONE_B.md](../MILESTONE_B.md) step B3 (owner-approved: base support logic for the 4v4). Owner rules from the
[community record](2026-09-27-community-discussions.md#how-positions-play-decision-rules-stated): the base keeps the
carrier's Force up (sk, respawn near the carrier, TE once or twice, sk again), chooses one plan, plays dark.
Build `basesup-build-20261002-c` (branch `b3-base-support`), worlds `.codex-research/basesup-20261002-a/`.

## Built

- **Loadout.** Base D (`Defender_v2`, `Defender2_v2`) has its own personality,
  `tools/fixtures/movement/defender/kejim_base.jkb`: dark side, team energize 3, drain 3, grip 3, with push 3, pull 3
  and levitation 3 kept: 60 of rank 7's 100 points (`bgForcePowerCost`). Chase D keeps `kejim_defender.jkb`. The
  personality is staged by `play_vs_runner.stage()`, `defense_laps.py` and `movement_bench.py`. It is a file, not a
  cvar, so both arms below carry it; `Defender_v2` as the one-sided or solo defender carries it too (the new powers are
  pressed only by the support plan).
- **Support plan** (`Support`, `bot2_defender.c`; `bot_baseSupport`, default 0). With a teammate (runner or camper)
  carrying the enemy flag within `bot_baseSupportRange` (2,000) of the own stand, and, while the own flag is home, only
  when that carrier is short of Force, support is base D's one plan: no chase, block or guard. It walks to the carrier
  (its last grounded position) until within `bot_baseSupportStand` (300) and in PVS, keeps shooting (Engage, which now
  prefers a visible enemy near its carrier to an unseen enemy carrier), and presses team energize
  (`GENCMD_FORCE_FORCEPOWEROTHER`) when the carrier has under `bot_baseSupportForce` (50) Force and is in the power's
  reach. A throw chosen the same frame keeps the frame's one `generic_cmd`.
- **Self-kill for support** (`bot_baseSupportSk`, default 1 under the plan): the carrier still short and base D under
  TE's cost within `bot_baseSupportSkNear` (600) of it ("spent"), or more than `bot_baseSupportSkFar` (3,000) away with
  the spawns under 0.6 of that ("far"); only when the team's spawns average within `bot_baseSupportSkSpawn` (2,500) of
  the carrier, at most every `bot_baseSupportSkMs` (4,000), never within 1 s of a spawn or with a flag.
- **Lines:** `SUPPORT_START`, `SUPPORT_END`, `SUPPORT_TE` (receiver's Force before and after; `fired=0` when the game
  refused the press), `SUPPORT_SK`. `tools/laps_support.py` reads them with the play CSV per arm.
- **Not built:** the carrier side (step 3). Carriers already wait at their own stand with the flag, and the presses
  fired with the carrier a median 88 u from its stand (quartiles 22 and 844), so no runner change was needed for TE to happen.

## Mechanics as this game has them

- Team energize here is vanilla `ForceTeamForceReplenish`: PVS **and** 256 u (x1.5 level 2, x2 level 3: 512 u), a 2 s
  debounce, 50 Force to one teammate in range (33 to two, 25 to three or more). The community's "reach by PVS, not line
  of sight" is base_enhanced; under this game's 512 u radius the base has to walk to the carrier.
- A press cost base D 25 Force in the worlds (self_force falls 50 -> 25 -> 0 per press). Base D often starts support
  with 5-20 Force (its throws and jumps spend it), hence the self-kills.

## Measured (4v4, Kejim, 25x, ten minutes a world, one at a time, 10 worlds an arm)

Common: `--two-sided --camper --roles base,chase --chaser-ambush --defender-rearm --defender-reset --defender-mines 2
--set bot_defenderWeapons=8a,11,10a --set bot_chaserGoal=1 --set bot_defenderCarryLog=1 --set g_fixGolanDamage=1 --set
bot_defender=2`; arms `off` (`bot_baseSupport=0`) and `on` (`=1`), `.codex-research/basesup-20261002-a/abc/`. All 20
worlds distinct, no crashes, 157-161 s wall a world. Per 600 game s, mean (se); captures from the CSV per team:

| metric | off | on | on - off |
|---|---|---|---|
| captures, both teams | 6.68 (0.84) | 7.10 (0.80) | +0.42 +- 1.16 |
| blue / red captures | 4.52 / 2.16 | 2.47 / 4.63 | (see below) |
| TE presses fired (both bases) | 0 | 20.6 (1.9) | |
| Force given by TE | 0 | 999 (91) | |
| support self-kills | 0 | 16.6 (1.4) (159 spent, 9 far in 10 worlds) | |
| carrier Force, all carrying samples | 21.6 (7.7) | 33.6 (5.2) | |
| carrying time within 1,000 u of own stand, s | 86 (13) | 215 (40) | +129 |
| carrier Force there | 14.3 (2.2) | 55.6 (6.9) | +41 |
| carrier Speed share (all carrying) | 0.53 | 0.49 | |
| both flags held at once, share of time | 0.33 (0.07) | 0.42 (0.04) | +0.09 |
| base D deaths a world (not self-kills, both bases) | 17.2 | 11.3 | |
| runner weapon deaths (laps_compare, blue runner) | 14.15 | 11.63 | -2.52 +- 1.87 |

- Every press fired (no refusals); a press took the receiver from 19.8 to 68.3 Force on average (209 presses). The two
  bases supported alike (red 11.8 TE, 8.7 sk a world; blue 9.1, 8.1).
- Support does what the rule says: carriers at home hold four times the Force and base D dies a third less. It does not
  convert into captures: in this lineup a capture waits on the own flag coming home, which is chase D's job, and a
  better-supplied carrier mainly holds longer (both flags held 33% -> 42% of the time).
- **Red-blue swing.** Off: blue led in 9 of 10 worlds (4.52 against 2.16); on: red led in 7 of 10 (4.63 against 2.47).
  The per-team support figures are symmetric, and the coordinator's no-support 4v4 worlds
  (`.codex-research/b3-20261002-a/base4`) split either way, so this reads as world-to-world variance larger than ten
  worlds resolve; laps_compare's single-sided `team_caps` row (blue touches) follows the swing and is not the measure.
- Superseded: build b (`basesup-build-20261002-b`, `ab/`, 3+3 worlds, stopped) had base D under support targeting an
  unseen enemy carrier 6,000 u away while an enemy stood beside its own carrier; one of its worlds stalled for eight
  minutes (below). Build c adds the visible-enemy preference.

## Verdict

`bot_baseSupport` stays 0: measured working (TE output, carrier Force, base survival) but no capture gain inside
+-1.2 a world and more standoff. The owner decides whether holding power is wanted for B3's scoring (hold times, the
stalemate) rather than captures.

## Faults found

- **Whole worlds stall with every bot standing still** (both arms). `ab/on-3` (build b, support on): from 1:35 no
  kill for eight minutes; red chase D's only rearm item was charged to a fall (`REARM done=fell`, skipped for good), so
  it stood unarmed 46 u from the blue carrier (`CARRY_SAMPLE ... pre=weapon weap=noweap`) while both carriers waited
  at their stands with full Force. `abc/off-1` (support off): from 2:35 no kill; blue chase D held the red flag on the
  red stand (a defender taking the enemy flag, which keep-out should prevent), the red carrier stood mid-map with 14
  health, and red chase D, in sight of that flag carrier 1,000 u off, never fired (`pre=gate`). Neither involves support;
  both look like the 4v4's idle states (B3's "both flags held and how it breaks").
- Base D's target choice prefers the enemy carrier even when unseen and far, so beside its own stand it ignores
  visible enemies (fixed only under support; the guard and chase plans still do it).
- Frame cost not measured in this step.
