# Bot2 review 2026-11-09, lane: defenders, aim, threat, objective AI and integration

Target: commit 6fe7bb2f, read from `.codex-research/wt-review-6fe7bb2f` only. Static reading; nothing was built,
launched or measured, so every cost figure below is an estimate from call counts and every outcome claim is
unmeasured.

Read in full: `bot2_defender.c` (3,332 lines), `bot2_defender.h`, `bot2_aim.c/.h`, `bot2_threat.c/.h`,
`ai_bot2_objective.c`, `ai_bot2.c`, `ai_bot2.h`, `ai_bot2_internal.h`, `bot2_commands.c/.h`, `bot2_svcmds.c/.h`,
`bot2_movebench.c/.h`, and the Bot2 hooks in `ai_main.c`, `g_main.c`, `g_active.c`, `g_svcmds.c`, `g_cmds.c` (`G_Kill`).
Movement and graph code was read only where this lane calls it.

All paths are under `codemp/game/` unless they start with another directory. "Changes outcomes" means a fix would
change what bots do in a laps world and needs a paired replay; "latent" means it needs a setting or situation the
retained campaigns do not use.

Counts: 24 findings. P0 0, P1 0, P2 3, P3 21. By category: bug 11, suggestion 6, perf 3, quality 4.

## Findings, most severe first

### BA-1 bug (policy gap), P2, gap confirmed, impact plausible: a carrier honours a self-kill request wherever it is

- Where: `ai_bot2_objective.c:957-960` (request), `ai_bot2_objective.c:1011-1014` (runner honours),
  `bot2_defender.c:2955-2959` (camper honours).
- Defect: `RunnerCamp` asks the team's carrier to `/kill` when the requester is within 600 u of the enemy stand with
  health 80+ and Force 75+, and the carrier has health under 40 or Force under 20 with Speed down. Neither the request
  nor the honour looks at where the carrier is. Force under 20 with Speed down is the normal state late in a carry.
- Failure scenario: 4v4, the camper takes the flag while its runner is dead. The runner respawns, reaches the enemy
  stand in about 10 s and requests. The camper, by then most of the way home, kills itself a few seconds from a
  capture; the flag goes back to the enemy stand and the runner starts a full carry.
- Fix: the carrier declines when its remaining route time home (`RouteMs` to its stand) is under a threshold, or under
  the requester's own expected carry time; log the decline. The requester can apply the same test before asking.
- Changes outcomes (camper and team captures). A count of `CAMPER_SK` and `SK_HONOUR` lines against the carrier's
  distance to its stand in retained logs would size it before any build.

### BA-2 bug, P2, asymmetry confirmed, consequence plausible: chase D walks to a dropped flag that is off the graph

- Where: `bot2_defender.c:1998-2003` (`DroppedFlag`), `bot2_defender.c:2009-2034` (`ReturnFlag`).
- Defect: `ReturnFlag` takes any dropped own flag as its goal and outranks every other errand. The runner filters the
  same flag through `ReachableDrop` (`ai_bot2_objective.c:18`, hazard and graph path), and the camper refuses a flag
  with no cell (`bot2_defender.c:2954`, "at the bottom of a pit"). Chase D has neither test. The file's own note at
  `bot2_defender.c:726` says a goal with no cell is a no-path plan every replan and that such holds were most of the
  defenders' falls.
- Failure scenario: the carrier dies over Kejim's chasm and the flag lies at the bottom until its native return.
  Chase D holds a no-path goal for that whole time and does nothing else.
- Second part, same function: inside 72 u it walks straight at the flag with no edge test (see BA-9).
- Fix: accept the flag only when `Graph_Nearest(flag,64,64)>=0` and no lethal `trigger_hurt` touches it, as the
  camper and runner do; otherwise fall through to the other errands.
- Changes outcomes (chase D falls and idle time after a carrier death over a pit).

### BA-3 bug, P2, confirmed, latent for worlds: `addrunner` leaves server-wide settings in the saved config

- Where: `bot2_svcmds.c:110-117,162`; `codemp/server/sv_init.cpp:1008,1010`; `ai_main.c:7644`.
- Defect: `addrunner` sets 20 cvars with `Cvar_Set`. `sv_autoDemo` and `sv_autoDemoMaxMaps` are `CVAR_ARCHIVE_ND`
  and `bot_telemetry` is `CVAR_ARCHIVE`, so one `addrunner` writes "record a server demo for every client, bots
  included, never prune" and telemetry on into the profile's config. Review item O19 covered only `bot_telemetry`
  and is still open; this is the wider form of it.
- Failure scenario: a play session from GameData runs `addrunner` once. Every later server session from that home
  records demos for all clients with no pruning, on any map, until someone resets the cvars. The drive-full
  incidents on this machine make that worth closing. Fixture runs use private homes and are not affected.
- Fix: register `bot_telemetry` without `CVAR_ARCHIVE` (closes O19); have `addrunner` restore the demo cvars at
  `G_ShutdownGame`, or set them only when `dedicated` is set, or leave them to the tools that want demos.
- Latent: no world outcome changes.

### BA-4 bug, P3, confirmed: an out-of-reach lob is fired anyway

- Where: `bot2_aim.c:82,122` set `BOT2_AIM_RANGE`; nothing reads it (`rg BOT2_AIM_RANGE` finds only those lines and
  the header). `Shoot` at `bot2_defender.c:1658-1760` tests only `BOT2_AIM_OK`.
- Defect: when the arc cannot reach, `Arc` returns 45 degrees and the solution is still `OK`. Repeater alt (1,100
  u/s) reaches about 1,500 u on level ground and less uphill; the shoot range is 1,500.
- Failure scenario: base D on a low post fires repeater alt at a runner 1,300 u away and 200 u above. Every shot
  falls short, each costing ammo and the 800 ms refire, and brings the next rearm walk sooner.
- Fix: treat `BOT2_AIM_RANGE` as a hold (`PRE_RANGE`) in `Shoot` and as a skip in `ChooseWeapon`.
- Changes outcomes (small).

### BA-5 bug, P3, plausible: the pistol fallback and the loadout chooser disagree about "has ammo"

- Where: `bot2_defender.c:2036-2042` (`Stocked`: three shots), `bot2_defender.c:1540-1541` (`ChooseWeapon`: one
  shot), `bot2_defender.c:2718-2733` (`PistolChoose`).
- Defect: with every loadout weapon under three shots, `PistolChoose` takes the pistol. Two seconds later the hold
  lapses and `ChooseWeapon` scores a loadout weapon with one or two shots above the pistol (the pistol is not in the
  loadout, so `current` is 0) and switches to it. Next frame `PistolChoose` switches back.
- Failure scenario: a defender left with one rocket fights an enemy holding a gun. Every 2 s one frame's
  `cmd->weapon` names the rocket launcher; Pmove starts a weapon change and then raises the pistol again, about
  450 ms without fire, and the pistol's 1,000 ms charge starts over. The rocket is never fired. Needs a loadout of
  two or more and a leftover in one of the four measured modes; against a drawn saber the pistol is not chosen and
  there is no flip.
- Fix: one definition. Either `ChooseWeapon` returns while `!Armed()` and the pistol is in hand, or the pistol is
  taken only when no loadout weapon has a shot left.
- Changes outcomes (small; 4v4 fights between gun bots).

### BA-6 bug, P3, confirmed: aim and perception state survive frames and lives they should not

- Where: `bot2_aim.c:393-417` (`Bot2_AimHumanStep`), `bot2_defender.c:3043-3047,3071-3084`, `bot2_defender.c:2791,
  2793,3119`.
- Defect, three parts:
  1. `Remember` runs while the defender is dead, so the history holds `visible` flags computed from the corpse. For
     `bot_defenderReactMs` after a respawn `Perceived` returns one of them. `Engage`'s lethal-throw branch trusts
     `seen->visible` with no current line of sight, so it can issue a throw (20 Force, a `MISS`) at a runner it
     cannot see from the spawn point.
  2. The respawn block resets rearm, mines, brace and movement but not the perception history, `engageSince` or
     the humanized view.
  3. `Bot2_AimHumanStep` keeps `view`, `desired` and `time` until the next call. Frames that skip `Shoot` without a
     reset (the saber walk, mine laying, the camper's carry, `CARRY_HOME`, death) leave them stale. The next step
     applies `pursuit = desired - old desired` to the old stored view, capped at `turnRate x 0.1 s` (50 degrees in
     styles 1 and 2), then writes that as the view: one frame pointed somewhere unrelated to the body's real view.
     The same happens in small on frames where `SetView` holds the view (a committed sequence) while the step still
     advances: the bot tracks "in its head" and snaps when released.
- Fix: clear `seenCount` and call `Bot2_AimHumanReset` in the respawn block; skip `Remember` while dead; in
  `Bot2_AimHumanStep` treat a gap over about 100 ms since the last step as a fresh acquisition from the caller's
  current view (pass it in), and reset on a held frame.
- Changes outcomes (small: first shot after a respawn or a saber walk). `bot_defenderViewLog` would show part 3.

### BA-7 suggestion, P3: a hit throw locks out the next throw for 4 s

- Where: `bot2_defender.c:2805` with `Follow` at `bot2_defender.c:1265-1322` (`DEFENDER_OUTCOME_MS` 4,000).
- Defect: "one throw followed at a time" is a logging constraint that gates behaviour. After any hit that does not
  kill, `outcomeDone` stays 0 until 4 s have passed, and neither a lethal throw nor the carrier pull is considered.
  The hand extend is over in about a second and a full pool pays for five throws.
- Scenario: chase D pulls the carrier, the carrier lands 300 u from a pit edge with no escape; the lethal push is
  not evaluated for 4 s.
- Fix: let a new throw close the open window as `superseded` (one more `OUTCOME` result), or keep a second
  follow slot.
- Changes outcomes (defender kills; the owner's "pull, then shot" window is separate and unaffected).

### BA-8 perf, P3, confirmed lossless: mode 2 counts all eight escapes when it only needs one

- Where: `bot2_defender.c:1084-1092` (`Escapes`), used at `bot2_defender.c:2830-2831`.
- Defect: `bot_defender` 2 (the default) discards a throw as soon as `escapes` is nonzero, and nothing else reads
  the count in mode 2 (`THROW` prints it only for a throw taken, where it is 0; `STARVED` likewise). `Escapes`
  still runs all eight sweeps, each up to 120 hull traces plus an `EntitiesInBox` and contact test per step.
- Cost: on a frame where a passive throw is lethal but escapable (a runner near a pit edge, the common case on
  Kejim), up to 2 kinds x 8 sweeps where 2 x 1 or 2 would answer. At 3,000 ms horizon that is up to about 1,900
  trace steps a frame for one defender, every frame the condition holds, with no throttle.
- Fix: an `EscapesAny` that returns at the first non-lethal direction, used when mode is 2 and patience is off.
  Same decisions, same log lines. Related and not lossless: the whole throw evaluation and `ChooseWeapon` (four
  traces a loadout entry, every frame once its 2 s hold has lapsed) could run every other frame.
- No outcome change for the early-out; replay equality is the check.

### BA-9 suggestion, P3: the straight "last few units" walks have no edge test

- Where: `bot2_defender.c:2025-2027` (flag return), `2181-2184` (`Rearm`), `2444-2446` (`RearmPlan`), `2531` (mine
  spot), `3008-3010` (camper take and retake).
- Defect: within 72 u each of these replaces the movement's command with forward 127 straight at the point. The
  saber walk did the same and ran defenders over Kejim's lip until `bot_defenderMeleeEdge` gave it
  `Move_GuestEdgeAhead` (`bot2_defender.c:2764`; base D falls 3.38 to 1.31 a world, 2026-10-06 record). These five
  sites were not given it. The mine-spot walk also skips `StraightOk`, so it alone may cross the flag keep-out box.
- Fix: one helper for the reach walk that applies the keep-out and the guest brake test, used by all five.
- Changes outcomes (defender falls near items and dropped flags on ledges).

### BA-10 bug, P3, confirmed, log only: the camper keeps deciding after it kills itself

- Where: `bot2_defender.c:2955-2959` returns 1 after `G_Kill`; `bot2_defender.c:3117-3128` then runs `Engage` and
  `Bot2_ProtectCommand` on the dead body.
- Scenario: `Engage` can count and log a `THROW` (then a `MISS`) or a `FIRE` press for a dead camper, which the
  campaign tools tally. `Reset` (`bot2_defender.c:2481`) returns early and has no such tail.
- Fix: return a distinct value from `CamperMove` after the kill and leave the frame.
- Latent for outcomes; changes log counts.

### BA-11 quality, P3, confirmed: the protect press is logged whether or not the game can take it

- Where: `ai_bot2_objective.c:773-784`.
- Defect: `ProtectDecide` checks only the Force cost. A client without protect has level 0, whose cost row is
  out of reach, so it prints `PROTECT_SKIP reason=force` every 2 s for as long as it is under fire. A client that
  has it but cannot use it now (hand extended, the power's debounce) prints `PROTECT` and spends the 300 ms press
  slot on a press the game refuses, so `PROTECT` lines overcount activations. The sibling lane reports the same
  for the Speed purchase (BM-14).
- Fix: require `forcePowersKnown` and `WP_ForcePowerUsable(ent,FP_PROTECT)` before the press; count a press as
  raised on the next frame's `forcePowersActive`.
- Latent for outcomes unless a refused press delays a usable one; log counts change.

### BA-12 perf, P3, confirmed: flag state is rediscovered by entity scans several times a frame per bot

- Where: `bot2_defender.c:2904-2912` (`EnemyFlag`), `1998-2003` (`DroppedFlag`), `781-787` (`KeepOutFlag`, called
  from `FlagBoxWatch`, `FlagKeepOut`, `StraightOk`, and through `Bot2_DefenderKeepOutBox` from `ForbidSync` at
  `bot2_move.c:3187,7561,7917`); `ai_bot2_objective.c:92-98` (the runner's two dropped-flag scans).
- Cost: each is a `G_Find` over every entity by classname. `Bot2_BaseFlag` was cached on 2026-10-03 for this
  reason; the dropped-flag lookups were not. I count 5 to 6 scans a frame for a base or chase D, 2 to 3 for a camper
  and 2 for a runner: roughly 30 a frame in a 4v4, on the order of 0.2 to 0.4 ms of a 25 ms frame at about 1,000
  entities. The G_Find perf counters at `g_utils.c:247` can confirm.
- Fix: one flag-status record per team per game frame (stand entity, dropped entity or none, carrier client or
  none), filled on first use and keyed by `level.time`, shared by every bot. It also gives BA-16 its carrier.
- Lossless if the record is built from the same tests.

### BA-13 suggestion, P3: decisions read enemy state the bot cannot perceive

- Where: `bot2_defender.c:1099-1106` (`CounterLevel`: the target's `weaponTime`, `pers.cmd`, Force and debounce
  through `WP_ForcePowerUsable`), `2473-2480` (`Reset`: the runner's live position), `2508` (`LayMines`: the
  same), `2952` and `2964` (camper: live health of its target and mate), `ai_bot2_objective.c:959` (a teammate's
  Force pool).
- Defect: `bot2_aim.h` states the rule (policy passes perceived state; the kernel never reads the entity), and the
  runner's side keeps a perceived Force estimate per threat for exactly this. The defender's lethal-throw rule
  instead knows whether the runner's counter will fail this frame, and the self-kill reset and mine laying know
  where an unseen runner is.
- Fix: `CounterLevel` from the perceived sample plus the runner-side style of Force estimate; `Reset` and
  `LayMines` from `Known()` and the kill feed.
- Changes outcomes (throws would be taken on less certain counters).

### BA-14 suggestion, P3: lobs are not checked against the world, and a grounded lead runs through walls

- Where: `bot2_defender.c:1684` (the line-of-sight test is skipped for `BOT2_AIM_LOB`), `bot2_aim.c:73-74`
  (`Predict`, grounded: straight line at constant height).
- Defect: repeater alt is fired whenever the target is seen and in range, with no test that the arc clears a
  lintel or ceiling; the flechette arc test (`GolanClear`) exists but only under `bot_aimGolan`. For a grounded
  target the lead point can sit inside a wall or past a ledge; the direct shot is then refused as blocked while the
  runner is in plain view.
- Fix: a short arc sweep for every lob (the existing `GolanClear` generalised to the mode's speed and lift); clip
  the grounded prediction with one hull trace along the travel.
- Changes outcomes (ammo use and hit rate).

### BA-15 bug, P3, confirmed, latent: defenders ignore a human enemy as soon as one enemy bot exists

- Where: `bot2_defender.c:562-568` (`TargetCandidate` accepts bots only), `630-652` (`DefenderRunner` falls back to a
  human only when no candidate exists).
- Scenario: the owner on offense with a camper bot as teammate. The defenders fight the camper only; the owner
  takes the flag unseen, and chase D never chases because `chaseOn` reads the target's powerup.
  `play_vs_runner.py --offense` kicks the runner and adds no camper, so today's tool never builds this world.
- Fix: accept any connected enemy on red or blue in `TargetCandidate`.
- Latent.

### BA-16 suggestion, P3: "the enemy I fight" and "who has my flag" are one pointer

- Where: `bot2_defender.c:3154,3167` read the carry from `runner`, which is `ChooseTarget`'s pick.
- Defect: for base D the carrier is the target only when seen or within hearing. With another enemy as target, the
  theft frame is missed (`theftWasHome` is updated regardless) and `chaseOn` stays false even inside the leash.
  Base D also never touches its own dropped flag: `ReturnFlag` is chase D's alone (`bot2_defender.c:2011`), so a
  flag lying 100 u from base D is left for the runner to take again.
- Fix: read the carrier from the per-frame flag record (BA-12) for theft, leash and chase; let base D return a
  dropped flag inside its leash.
- Changes outcomes.

### BA-17 suggestion, P3: protect has no reaction time

- Where: `ai_bot2_objective.c:632-652,741-748`.
- Defect: movement cannot see a missile for `bot_threatReactMs` (850), but `RunnerIncomingMissile` sees it at age
  0, including one `bot2_threat.c` has unlinked (the sight trace passes through and reads as visible). Protect is
  raised 0 to 150 ms after a rocket appears inside 600 u. That is reactive, as the owner requires, but faster than
  a player.
- Fix: an age gate from the threat module's first-seen table (expose `Bot2_ThreatAge(ent)`), default about 250 ms.
- Changes outcomes (runner and camper weapon deaths); an owner decision.

### BA-18 quality, P3: `bot2_defender.c` structure

- 3,332 lines, one 18 KB state struct per slot behind `#define defender (*defenderCur)`, 94 distinct `bot_*` cvars
  read by string at 133 sites, none registered (a mistyped name silently takes the default, and no command lists
  them). The first 300 lines are a changelog; current behaviour has to be reconstructed from dated notes.
- Duplication: `Remembered` (`:1327`) is `Perceived` (`:1184`); the reach tail (aim, forward 127, or `DefenderMove`
  plus button and `generic_cmd` filter) appears nine times with small differences that are bugs (BA-9); the
  own-flag powerup ternary appears 12 times; the `generic_cmd!=GENCMD_FORCE_SPEED` filter 13 times across the two
  policy files; `Rearm` and `NearestItem*` are a second rearm implementation kept "byte-identical" behind
  `bot_defenderRearmPlan 0`; `ThrowVelocity`, the hull sweep and the hurt test exist again in
  `ai_bot2_objective.c:320-366` with different lethality rules.
- Dead or stale: seven unused `vec3_t aim={1,0,0}` locals; `Wants(self,int unused)`; a dead store at `:1716`;
  `bot2_defender.h` still describes one defender and the removed lattice mode; `ai_bot2.h:38` names the removed
  `ai_bot2_movement.c`; the struct comment at `:377` lists `dead` as a throw outcome though it is the defender's
  own death flag.
- Gravity comes from five places: 800 literals (`:1436`, `:2840`), `400*s*s` (`:2610`), `DEFAULT_GRAVITY`
  (`bot2_aim.c:25`), `g_gravity.value` (`:1508`), `ps->gravity` (`:1060`). Equal only while `addrunner` forces 800.
- One frame-length constant: `clampTime<=50` (`:1233`) means "last frame" only at 25 ms and 50 ms frames.
- Proposal: split by job (perception and target; throw model shared with the runner's exposure test; shoot and
  weapon choice; rearm; role movement; camper), with a settings struct read once a frame and printed once at the
  release, which also gives the campaign tools the frozen settings they bind to. Move the history into dated
  records.

### BA-19 bug, P3, confirmed, latent: review item O17 still holds

- Where: `ai_bot2.c:79-92`. `since[]` is cleared on death and on a not-stranded check but not on connect. A bot
  that leaves while stranded and alive hands its timer to the next occupant, which can be killed at its first
  stranded check. `BotAISetupClient` calls `Bot2_ClearState`; clearing it there closes it.

### BA-20 quality, P3: per-client statics outside `Bot2_ClearState`

- `aimView[]` (`bot2_aim.c:373`), `runnerCapturesSeen[]` (`ai_bot2.c:37`), `meleeSince[]`, `dropChecks[]`,
  `runnerCamp[]`, `skRequest[]`, `goalChainIndex[]` (`ai_bot2_objective.c`) and `defenders[]` are each reset by
  their own rule or not at all. I found no failure beyond BA-6 and BA-19: each is either re-keyed (entity, name,
  `enterTime`) or self-heals within a second. The contract in AGENTS.md ("some controllers also own private
  per-client state") is the risk; a single `Bot2_ClientReset(client, reason)` that every module registers with
  would make the next one safe.

### BA-21 bug, P3, plausible, log only: a throw's hit is recognised by a timestamp any thrower can write

- Where: `bot2_defender.c:1276` (`pushEffectTime==fireTime+600`).
- Scenario: base and chase D throw at the same carrier on the same frame; one is out of arc. Both log `HIT`, both
  open the follow-up window (`followUntil`) and, with BA-7, both lock out. Rare.
- Fix: also require the target's `otherKiller`/`ps.otherKillerTime` or a velocity change consistent with this
  thrower's `ThrowVelocity`.

### BA-22 perf, P3, small: per-frame work that could be per-change

- `bot2_threat.c:41-47` walks 992 entity slots on every hide; with the play frame, each bot's Pmove boundary and
  the frame-end pool that is about 17 walks a frame in a 4v4. Bounding the loop by `level.num_entities` (and
  clearing above it once) is free.
- `ai_bot2.c:159-183` runs twice a frame in play mode (`BotAIStartFrame` and `Bot2_RunPlayFrame`) and fetches the
  userinfo string of every bot that has no custom think (stock bots) each time.
- `bot2_defender.c:2234-2257` (`RearmObserve`) walks every entity each frame a defender is rearming.
- `bot2_aim.c:279` reads `bot_aimGolan` on every solve, `bot2_defender.c:484-502` reads `bot_movementOnly` on
  every `DefenderSlot` call, which the movement makes per simulated step.
- Together a few tens of microseconds a frame; worth doing only with the settings struct of BA-18.

### BA-23 quality, P3: `Melee` writes the player state

- Where: `bot2_defender.c:2748` sets `ps->saberHolstered=0` directly, and `DefenderPlace` (`:701`) sets it to 2.
  Everything else the defender does goes through the command. A holster toggle through the command
  (`GENCMD_SABERSWITCH`) would keep the "native resources and lifecycle" rule; it costs the frame's one
  `generic_cmd`.

### BA-24 bug, P3, latent: a defender's stands are fixed at the release

- Where: `bot2_defender.c:891-896`. `stand` and `enemyStand` are read once in `GuardSetup`. A defender moved to
  the other team (`forceteam`) keeps guarding its old stand and treats its new flag as the enemy's. Fixtures never
  switch teams. Re-read on a team change, or key the release on `sessionTeam`.

## Earlier review's items, re-checked

- O17 (`StrandedKill` slot reuse): still open, BA-19.
- O19 (`bot_telemetry` archived): still open and wider than recorded, BA-3.
- D1 (missile scan per hide): correct. Every hide I found runs at real level time (`ai_bot2.c:113`,
  `bot2_movebench.c:58`, `bot2_move.c:7570`), so the simulated clock of rollouts never reaches `firstSeen`.
- D2 (`rpWayEvalAt`): correct; the way-bounded evaluation then keeps an item the fallback chose, as intended.
- D3 (`Follow` scores `fireRunner`), D4 (third defender refused), D5 (alt flag parse), D6 (pitch clamp), D7 (charge
  only for charging alts), D8 (`rpClaimedCheckAt`), D9 (256-frame history), D10 (`Mate`, `Bot2_DefenderHops`, late
  release), D11 (`freetime`): each fix is present and does what its row says.
- O7 to O10 (absorb projection, mine slots, `ReachableDrop` budget, stale `exposed`): present and correct. O11
  (`CheckForTriggerHurt`) is in `ai_bot2_movement.c`, not read here.

Checked clean: array bounds of every per-client and per-entity index in the five policy files; format strings
against their arguments; `SetView`'s command rotation (keeps world direction and wish speed) and angle wraps; the
arc solver and the OU error update (both independent of frame length); `ThrowVelocity` against the rule quoted from
`w_force.c`; hide and relink pairing; `botat`, `botplayrate`, `botkill`, `adddefender` argument handling; the nav
commands; the play dispatch order (thinks before any client's Pmove, so a throw lands in the frame it is issued and
`Follow`'s 50 ms miss window holds at any frame length).

## Needs a high-effort follow-up

1. BA-1: size it from retained logs (`CAMPER_SK`, `SK_HONOUR`, carrier position at the kill), then a paired arm with
   the route-time guard. It is the only finding here that plausibly costs captures every world.
2. BA-2 and BA-9 together: count defender deaths within a few seconds of `RETURN`, `REARM` and `MINE` lines in
   retained worlds; then one build with the shared reach helper and the dropped-flag test.
3. BA-7 and BA-13 are behaviour changes the owner should decide; each wants its own arm.
4. BA-8: a replay-equality check of the early-out, then the frame meter's sweep counter before and after on Kejim.
5. A measured per-frame cost split of the defender layer (sweeps, aim solves, entity scans) apart from the
   movement it calls. I could only count calls; the 2 vCPU target needs numbers.
6. BA-18's split is large and touches frozen-source rules; plan it between campaigns.

## Not covered

- Anything measured: no build, no world, no frame meter. Costs are call counts times assumed trace cost.
- `bot2_move.c`, `bot2_graph.c`, `bot2_forecast.c` and the other kernels beyond the signatures this lane calls;
  what `Bot2_ExecuteMovement` does with an off-graph goal (BA-2 relies on this file's own note, not on a reading of
  the planner).
- Native `w_force.c` and `bg_pmove.c` beyond the lines cited (weapon-change timing in BA-5 is from memory of
  `PM_Weapon`, hence "plausible").
- `bot2_corner.c`, `bot2_probe.c`, `bot2_route_*.c`, `bot2_kick.c`, `bot2_movecost.c`, `bot2_servercost.cpp`
  (area 8 of the earlier review; only their calls into this lane were read).
- The Python tools that parse `[DEFENDER]` lines; BA-10, BA-11 and BA-21 change counts they read.
- `.bot` and `.jkb` files (whether the camper has protect, BA-11) and the fixture cvar sets the campaigns pass.
