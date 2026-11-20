# Review batch 2: outcome fixes behind cvars (2026-11-10)

Owner, 2026-11-10: "Start batch 2", the 2026-11-09 review's open bugs that can change a world. Each fix is behind its
own cvar, default 0, in 8a2b00c3 on `claude/bot2-review-next`. The movement and defender parts were written by two
subagents and reviewed here.

## Fixes (each cvar 0 = before)

| Cvar | Finding | When on |
|---|---|---|
| `bot_moveFixBP9` | BP-9 | The graph's clocks (edge time-outs and their return, fold and spread timers) read the real frame's time (`Graph_Frame` from `G_RunFrame`), not a pre-search's predicted `level.time`. |
| `bot_moveFixBP12` | BP-12 | A rollout gives a boon holder 6 Force a tick, as `WP_ForcePowerRegenerate(self, 6)`. |
| `bot_moveFixBM5` | BM-5 | An objective change waits while a knock recovery or a lift ride is in progress (the hold still needs `ArriveSafe`). |
| `bot_moveFixBM7` | BM-7 item 3 | A pad state entered before the bot's last spawn goes back to walk. Items 1 and 2 are in `ai_bot2_movement.c`, not done. |
| `bot_moveFixBM15` | BM-15 | The executor's `dt` is capped at two steps, and a walk ends if `Move_Command` last ran more than two steps ago. |
| `bot_moveFixBX3` | BX-3 | A cached rerouted winner with no route cells is re-rolled as the judge's first pass ran it. |
| `bot_moveFixBX4` | BX-4 | `WalkDefer`'s ground guard predicts the full `bot_moveDeferGround` frames. |
| `bot_moveFixBX5` | BX-5, hit part | An edge whose plan was knocked in flight is not charged to the world as "walked". The "lands short" part is an owner question, not done. |
| `bot_moveFixBX10` | BX-10 | A guest's commit rollout resets the stop state and runs in its own guard scope. |
| `bot_defenderFixBA4` | BA-4 | An arc solution out of range is not fired, and that weapon scores nothing in `ChooseWeapon`. |
| `bot_defenderFixBA5` | BA-5 | `ChooseWeapon` keeps the pistol while no loadout weapon has ammo (no 2 s flip). |
| `bot_defenderFixBA6` | BA-6 | No sightings while dead; a respawn clears sightings, engage and the human aim; a gap of more than 100 ms or a held view resets the aim (done from `Shoot`, not in `bot2_aim.c`). |
| `bot_defenderFixBA10` | BA-10 | The camper leaves the frame after its self-kill (no `Engage` or protect on the body). |
| `bot_defenderFixBA21` | BA-21 | A throw's hit also needs the target's `otherKiller` to be this thrower, stamped by the same push. |
| `bot_protectFixBA11` | BA-11 | No protect press without protect known, and none the game would refuse now (`PROTECT_SKIP reason=unusable`). Counting raised presses is not done. |

Not done: BM-10 (in `ai_bot2_localnav.c`), BM-8 and BM-9 (they change every flight and what the graph learns), BP-7.

## Checks

- **Replay:** build `review-next-build-20261110-j` (8a2b00c3) with its defaults replays the new-defaults control on
  Kejim and Dantooine 35 and 36 (`batch2-20261110-a/identity-*.txt`, the branch's `laps_identity.py`).
- **Combined arm `b2`:** every cvar above at 1, paired by seed against `ndctl-20261110-a/worlds/nd-*`. 64 worlds,
  none failed (`batch2-20261110-a/`).
- **Dantooine split:** `b2m` (BP, BM, BX) and `b2d` (BA) on seeds 35-50 (`batch2split-20261110-a/`). 32 worlds,
  none failed.

## Results (arm minus control, per 600 game s)

| | Kejim b2 (48) | Dantooine b2 (16) | Dantooine b2m (16) | Dantooine b2d (16) |
|---|---|---|---|---|
| team captures | -0.22 ± 0.22 | +0.57 ± 0.39 | +0.11 ± 0.30 | -0.22 ± 0.30 |
| runners' falls | -0.98 ± 0.55 | +0.05 ± 0.71 | -0.81 ± 0.53 | -1.24 ± 0.54 |
| runners' weapon deaths | +1.82 ± 0.84 (inside ±15%) | -2.38 ± 1.34 | -0.28 ± 1.33 | -0.00 ± 1.24 |
| blue captures | -0.15 ± 0.36 | **+2.20 ± 0.58** | +0.67 ± 0.53 | +0.36 ± 0.60 |
| red minus blue | -0.24 ± 0.51 | **-3.26 ± 0.84** | -1.17 ± 0.88 | -1.11 ± 0.99 |

- Team captures: undecided everywhere, with no consistent sign.
- Runners' falls read down in three of four columns (-0.8 to -1.2, about 2 se), plausibly BM-5 (no goal change
  mid-recovery) and BX-5 (no world charge for a knocked edge).
- The Dantooine side shift passes the cut in the combined arm only. Each half carries about half of it, and neither
  passes alone. Every changed arm on these 16 Dantooine seeds reads blue-ward: today's spreading arm -0.98 ± 0.65,
  b2m -1.17, b2d -1.11. That suggests the control's 16 seeds happen to lean red (+1.35) and any change re-rolls
  them; it is not shown to be a fix. More Dantooine seeds for the control would settle it.
- Wall time per world was lower in every batch-2 arm (116 s against 157 s on Kejim) while the work meter's traces,
  steps and expansions stayed within a few percent. Machine load, not the fixes; not a timing measure.

## Reading

Nothing is decided. All fifteen cvars stay 0 until the owner chooses: take them as correctness fixes on this
reading (no capture harm shown; falls read down), or first add Dantooine seeds to the control and the arm (the side
shift).

## Default (owner, 2026-11-10)

Owner: "Turn them on". Since a971ed85 each of the fifteen cvars is on when unset (`FixOn` in each file); 0 turns that
fix off. Build `review-next-build-20261110-k` with its defaults replays the combined arm's worlds `b2-kej-35` and
`b2-dan-35` (build -j, every cvar set to 1) exactly (`batch2on-20261110-a/identity-*.txt`). So
`batch2-20261110-a/worlds/b2-*` (Kejim 35-82, Dantooine 35-50) is the control for later arms, replacing `nd-*`.
