# Fixture defender

One enemy bot, `Defender_v2` (`codemp/game/bot2_defender.c`), for the
one-defender pilot: it holds a post near its own flag stand and pushes or
pulls the movement runner when its own ballistic sweep says the throw puts
the runner into a lethal hurt volume. The runner is unchanged and knows
nothing about it.

- `kejim_defender.bot`, `kejim_defender.jkb`: the bot and its loadout (push 3,
  pull 3, levitation 3; bots take their powers from the personality's
  `forceinfo`). The saber is holstered at the post: the fixture throws and
  does nothing else.
- `kejim_base.jkb`: base D's loadout (`Defender_v2`, `Defender2_v2`; milestone B3,
  owner 2026-10-02): dark side, team energize 3, drain 3, grip 3 with push 3,
  pull 3 and levitation 3. Chase D keeps `kejim_defender.jkb`; `Defender_v2` as the
  solo pilot defender carries the base loadout too (the extra powers are not pressed;
  `bot_baseSupport`, which pressed team energize, was removed 2026-10-07).
- `posts.json`: the post per map and defending team, `x y z yaw`, dropped to
  the floor in the engine. Kejim's is measured (see `source`).

`tools/movement_bench.py --defender 1` stages both files, adds the defender
on the runner's enemy team (`adddefender`) and sets `bot_defenderPost`; the
corner start places it and fills its Force. `--defender 0` is the body
alone, no throws; `--defender 2` throws only when no held air-input direction
lets the runner reach floor (mode 1 throws on the passive flight). The post
is taken once: a killed defender respawns where the game puts it and throws
from there. The runner's way through a body is the pressed-carrier melee:
`--set bot_carrierMelee=1 --set bot_carrierMeleeRange=96`. `--defender-react MS` perceives the runner that long ago.
Every throw, native hit, miss and outcome is a `[DEFENDER]` line in the
world's `server.log`, summarised in each case's `result.json`.
From the owner's 1v1, off by default (records/2026-09-29-defender-learnings.md):
- `--defender-rearm`: after a death, walk to items that restore the loadout.
- `--defender-reset`: a self-kill reset, used only when respawning gets it
  home much faster than walking.
- `--defender-mines N`: base D keeps proximity mines on N of `posts.json`
  `mine_spots`.
- `--chaser-ambush`: chase D waits on `ambush_posts` ahead of the runner.
- `--defender-patience`: an escapable lethal throw waits for a better frame.
- `--defender-walk`: errands are walked; hop searches run only while chasing.

Rearming treats the loadout as any-of: one stocked weapon is armed. The laps
worlds use the owner's `--set bot_defenderWeapons=8a,11,10a`.

`tools/defense_laps.py` runs laps worlds with both defenders over many lives.
Since 2026-09-29 the runner has push 3 and pull 3 (`strafe_play/kejim_runner.bot`).
It counters throws on the ground only, and the defender's throw forecast models
that counter. `HIT` logs `counter`, `predicted_counter`, `model_h` and
`native_h` (records/2026-09-29-runner-throw-counters.md).

`--defender-chase` makes it chase the carrier from its post once the runner
carries its flag. Alone it walks; with `--defender-strafe` it runs the
runner's own searches (hops, strafe chords, drops, pads) as a guest: it writes
nothing into the shared graph (no learned edge, validation, cost or failure
charge; its failures go on its own avoid list) and never arms, ends or feeds
the runner's parity harness, decision tape or plan directives. Its goal is the
carrier as last seen, replaced only when a sighting is `bot_defenderRetarget`
units (default 256) away or the goal is reached, since every goal change resets
its movement. It has levitation 3 and no Speed unless `--defender-speed`
(`bot_defenderSpeed`) gives it Speed 3 at its release; it then buys Speed as the
runner does, from the Force its throws use, and a lethal throw it cannot
afford is logged as `[DEFENDER] STARVED`. `--frame-meter` records each
bot's movement time per frame (`frame_cost` in `result.json`); it is
wall-clock, so run those worlds with `--parallel 1`.
(records/2026-09-28-strafing-chaser.md)

The base defender (records/2026-09-28-base-defender.md): `--defender-guard`
holds the `guard_posts` of `posts.json` (the demo study's base posts) between
chases; `--set bot_defenderWeapon=N` gives it weapon N and it shoots the
perceived runner (`FIRE` per shot that spent ammo, `DAMAGE` per hit);
`--set bot_defenderPullCarrier=D --set bot_defenderPullMax=M` pulls the carrier
between D and M units (`THROW ... reason=carrier`), `bot_defenderComboMs` only
right after a shot. The study's settings: rocket (11), pull 350-700,
`--defender-react 250`.
Outside the bench, in a laps world (`tools/play_vs_runner.py --no-client`), `rcon adddefender <team>`
and then `rcon defenderpost` places it at `bot_defenderPost` (again to move it;
`kick Defender_v2` removes it). Its reply carries the `POST` line, not
`server.log` (records/2026-09-28-failure-attribution.md).

Two defenders (records/2026-09-29-base-chase-defenders.md): `--defender-roles base` makes
`Defender_v2` base D (guards, fights the carrier within `bot_defenderLeash` of its stand);
`--defender-roles chase` adds `Chaser_v2` (`adddefender <team> chase`) as chase D (holds
`chase_posts`, intercepts on the carrier's route home; `INTERCEPT` lines); `base,chase` both.
