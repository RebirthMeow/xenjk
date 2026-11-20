# Play environment and threat reaction (2026-09-26)

Owner, after playing against the runner: push/pull could not be spammed and Force Speed
sometimes did not activate; the runner seemed to air-brake away from a rocket fired on a
perfect intercept. Owner direction: push/pull must work as on a hosted base_enhanced
server; dodging is fine but reaction to moving threats needs a human reaction time, while
fixed geometry needs none.

## Dropped force presses: our server setting, not the game

`addrunner` forced the server-wide `pmove_fixed 1` (`pmove_msec 8`) for the runner's
deterministic physics. `ClientThink_real` rounds each command's time up to the 8 ms grid,
so the next command from a client faster than 125 fps is older than the player's command
time and returns early (`msec < 1`), discarding the whole command. Held buttons survive;
a one-shot generic command (push, pull, speed) does not.

Probe (`bot_probeGenCmd`, scripted client spamming push every third frame at 250 fps,
`.codex-research/probe-20260926/probe_push.cfg`):

| Server | Sent | Arrived | Reached the push rule | Available but did not fire |
|---|---:|---:|---:|---:|
| `pmove_fixed 1` (session `play-vs-runner-20260926T105031Z`) | 48,278 | 48,028 | 24,144 (half discarded) | 0 |
| bots-only fixed (`...T112208Z`) | 20,422 | 20,266 | 20,257 (the 9 others: first 3 frames after joining, still spectating) | 0 |

Every other push that reached the rule failed only for base-game reasons: Force under 20,
the previous push's hand animation, weapon cooldown (`w_force.c` `ForceThrow`). Spammed,
push fires every 4.00 s, the Force regeneration for its 20 points.

base_enhanced (Avygeil/base_enhanced, read at f141eac) keeps the stock early return and
nothing re-queues a lost generic command, but its default is `pmove_fixed 0`, where each
command advances the command time by its own length. **Fix:** `addrunner` now sets
`bot_pmoveFixed 1` instead of the server's `pmove_fixed`; the play frame sets
`client->pers.pmoveFixed` on Bot2 bots (the per-client flag `ClientThink_real` already
honours), and every Bot2 read of the cvar goes through `Bot2_PmoveFixed()`. The runner's
physics are unchanged: Kejim L2 class 150 `--skip-twins`, 8/8 times identical to the
sweep's control with the server at `pmove_fixed 0` (the old world archived 1), parity 0.

base_enhanced's records standard (`g_main.c` 7409-7424) also names `sv_fps 30` and
`g_gravity 760`. Owner: not worth chasing - gravity is not compensated for the tick rate, so
the 760 only corrects jump height at 30 fps. The pmove step is what matters for play.
`play_vs_runner.py` sets `pmove_fixed 0` explicitly.

## Rockets: the runner saw them as walls instantly

A rocket's contents are `MASK_SHOT` (`CONTENTS_SOLID|CONTENTS_BODY`), solid to every movement
trace. Probe `bot_probeMissileTrace` counts runner traces that hit a missile, by phase;
`probe_rocket <client> cross|head [distance]` spawns a rocket with no damage or splash.
96 rockets each run (`.codex-research/probe-20260926/rocket_loop.py`):

| Build | Planning hits | Youngest rocket planning reacted to |
|---|---:|---:|
| before (`...T110307Z`) | 15 frames (5 episodes: plans dropped, one strafe edge marked stale, 562 to 359 u/s) | 100 ms |
| `bot_threatReactMs 250` (`...T111504Z`) | 14 frames | 275 ms (0 under 250) |

**Fix:** `bot2_threat.c`. While a bot decides (the play frame and its pre-Pmove movement
hooks), missiles first seen less than `bot_threatReactMs` ago (default 850 - owner, the 768-unit Kejim bolt-to-power-cell flight at 900 u/s; measured at 250 above; 0 instant) are
unlinked and relinked afterwards. Its physics step is untouched: the body still collides.
Players are also moving obstacles the runner sees instantly; not changed.

## Identity of the bots-only fixed step

`.codex-research/pmfix-identity-20260926`: build `threat-build-20260926-d`, every fixture map,
class 150, `--skip-twins`, parity 256, with `--set pmove_fixed=1` (the old server-wide step)
against the default (bots only). 112 pairs: 110 identical in the batch; the two Taloraan L2
differences (red_out stand 13,900 vs 15,800, blue_out respawn5 8,875 vs 9,675) re-ran twice
in each arm at 13,900 and 8,875 - batch variation, not the change. Parity mismatches 0.
(The before-build arm could not run: `ai_bot2.c` lost its mixed CRLF/LF line endings to an
editor normalisation during this work - content unchanged, bytes not - so no snapshot of the
older sources matches its manifest.)

Builds: `probe-build-20260926-b`, `threat-build-20260926-a`, `threat-build-20260926-b`.

## Review follow-up, 2026-09-27

- **Fixed step outside mode 2.** `bot_pmoveFixed` reached a bot's `client->pers.pmoveFixed` only
  from the mode-2 play dispatcher, while `Bot2_PmoveFixed()` (every Bot2 forecast) reads the cvar
  in any mode: after `addrunner`, switching to `bot_movementOnly 1` and adding a bot gave
  fixed-step predictions and variable-step execution. The sync moved into
  `Bot2_UpdateManagedBots`, which `BotAIStartFrame` runs every frame in every mode before the
  bots' `ClientThink_real`.
- **Human route metadata.** `HUMAN_ROUTE1` meta recorded `Bot2_PmoveFixed()` for a human subject;
  it now records the subject's effective step (`pmove_fixed || pers.pmoveFixed`), as the CSV
  rows already did.
- **`tools/human_rush_study.py`:** `--demo` extract ids are a SHA-1 of the resolved path instead
  of the per-process `hash()`, and demos already extracted under an old id are skipped by path;
  bot trips are grouped by label in both strafe tables; `by_spawn.csv`/`by_player.csv`/`index.csv`
  have fixed headers, so a run with no qualifying pickups no longer raises. The retained
  `.codex-research/human-rush` report re-run on a copy is byte-identical in all five CSVs.
- **Identity** (`.codex-research/reviewfix-identity-20260927`): Kejim class 150, `--skip-twins`,
  parity 256, `--edges`. Control build `reviewfix-control-build-20260927-a` (commit `5fa681f2`)
  twice, fix build `reviewfix-build-20260927-a` once: 24/24 passed in each, outcomes and times
  identical in all three arms, parity mismatches 0. `straight_commits`/`learned` counts differ
  in 5-10 cases between any two arms, including the two control runs: run-to-run variation.
