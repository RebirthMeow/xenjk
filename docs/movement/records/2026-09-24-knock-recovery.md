# Knock recovery and the forecast temp-entity leak, 2026-09-24

Follows [the knockback fixture](2026-09-24-knockback-fixture.md). Owner scope:
a push straight into a pit is not a case to solve. Being put off course,
landing, and then falling into a pit because of it is.

## What was built

`bot_moveKnockRecover` (`codemp/game/bot2_move.c`; on by default since the
owner's decision after these measurements, 0 turns it off):

- **Hit detection.** A hit is a velocity change of more than 100 u/s between
  the end of one Pmove and the next command. Only the game changes velocity
  there. Teleports and respawns (the teleport bit toggles) and jump pads
  (`jumppad_ent` changes) are excluded. The native knockback timer was tried
  first and rejected, for two reasons. `G_ApplyKnockback` starts the timer only
  when no other timer runs, so a hit on a landing frame set none. And spawns,
  teleports and wall-jump rebounds all set it.
- **What a hit does.** It ends any committed hop or climb as a `knocked` miss.
  No edge is charged: the edge was not at fault. It then asks the boundary for
  a recovery. A hit on the ground waits up to 150 ms to lift the bot; one that
  never lifts it is left to the walk's momentum brake.
- **The recovery search.** Nine policies are rolled out through the forecast
  (native Pmove): eight air-control headings around the travel direction, plus
  a coast. Each holds its heading with forward pressed until grounded, then
  applies the walk's brake until the bot stands (under 40 u/s).
  - Admitted: the rollout ends standing on world floor over a graph cell,
    touching no trigger and leaving no simulation contract. So a landing in a
    hurt volume, on a mover, or one that slides off its ledge is refused.
  - Chosen: the admitted candidate that stands nearest the route the bot was
    knocked off, then the soonest.
  - Re-searched every 200 ms while airborne. A search that admits nothing is
    retried until the bot lands.
- **Execution.** The chosen policy's command is issued from the same function
  the rollout used. The committing frame is re-issued, and the recovery owns
  the frames until the bot stands, a 6 s bound expires, or the movement type
  changes. Then the walk replans from where the bot stands.

## The forecast leak it exposed

On Dantooine, 11 of 36 knock searches found every candidate refused because
the forecast guard reported contamination. A new diagnostic,
`bot_forecastContaminationLog`, named the change: offset 12 of `level` on the
x86 build, which is `level.num_entities`. `PM_CrashLandEffect` spawns a
landing-dust temp entity for any landing at 300 u/s or faster (vertically) on
mud, sand, dirt, snow or gravel; the liquid splashes do the same. A rollout
runs that code, so every hard landing on those materials put a real effect
entity into the live world. The forecast already refused such rollouts as
`BOT2_FORECAST_MATERIAL`, but only after the entity existed, and the guard then
refused every later candidate of the search.

The existing searches hit it too: 6 contaminated climb and edge searches in
one Dantooine arm with recovery off. The fix is a small upstream call:
`G_TempEntity` returns a scratch entity while a rollout runs
(`Bot2_ForecastSuppressTempEntity`). The material and liquid refusals are
unchanged; such rollouts are still unsupported, they just no longer leak.

## Measurements

Builds:

- a: `strafe-build-7651fce0b7944a0b8132b6c4067a1145`, recovery before the leak
  fix.
- b: `strafe-build-11fdd16756b0464b906bf7f792da24eb`, with the fix.

Worlds are under `.codex-research/knock-recover-20260924-a/` and
`-b/`, with pairings in `<arm>.report.json`. Kicks and settings are as in the
fixture record. Extra time is paired against a control, one world per case.

| Arm | Recovery off (a) | Recovery on (a) | Recovery on, leak fixed (b) |
| --- | --- | --- | --- |
| Kejim walk | 33/36, 3 after landing | 36/36 | 36/36 |
| ctf4 climb | 25/36, 9 direct, 2 after landing | 36/36 | 34/34 fired |
| Dantooine hop | 22/36, 14 direct | 25/36, 11 direct | 30/36, 6 direct |
| ctf4 hop | 27/36, 3 direct, 6 stalls | 34/36, 2 stalls | 33/36, 3 stalls |
| Kejim hop | 26/36 in the fixture run, 10 direct | 36/36 | 36/36 |
| Dantooine walk | 33/36 in the fixture run, 3 direct | 33/36, 3 direct | 36/36 |
| Dantooine climb | 35/35 fired in the fixture run | 34/35 fired | 33/33 fired |
| ctf4 walk | 36/36 in the fixture run | 36/36 | not rerun |

- **Deaths after an off-course landing** (the owner's case): 5 with recovery
  off on this build, none with it on in any arm of either build.
- **Most "direct" deaths are recoverable.** Air control steered 9 ctf4 climb
  pushes onto the ledge under the climb instead of into the void, and all 10
  Kejim hop pushes back onto the walkway. The fixture's `direct` class means
  no ground was touched before the hazard, not that none was reachable.
- **Dantooine hop's six remaining deaths are direct and out of reach.** With
  the retry, each world searched repeatedly the whole way down. Every
  candidate was refused as trigger plus liquid (`unsupported=0x108`): each
  reachable landing is in the chasm's water and hurt volume. This refutes the
  hypothesis, raised during the analysis, that the material refusal was
  blocking valid landings on dirt.
- **ctf4 hop's three stalls are the lift gap.** In two, every candidate ends
  grounded on a mover near (-200, 2992) at z=152 (`short=9` on every retry).
  In the third, recovery stood the bot at z=88, and from there the only way
  on is a lift the graph cannot model. The two ctf4 climb failures are the
  same lift stall with the kick unfired.
- **False triggers: none.** Recovery was on in all three unkicked controls on
  build a (108 worlds, lifts and jump pads included): zero hits, 36/36 each.
- **Floors on build b at defaults** (recovery off, leak fixed): Kejim, ctf4
  and Dantooine each 36/36. Dantooine's contaminated searches went from 6 to
  0.
- **Cost of the search:** 2,882 searches, 2.1 ms median of rollouts, 5.9 ms at
  p95, 10.7 ms at most.
- **Cost of the recovery.** A recovered world runs a median of about 0.4 to
  1.7 s slower than its control. The recovery brakes to a stand and the walk
  restarts from rest. This is one world per case, so it is a size, not a
  resolved effect.

## Open

- Planning from a mover floor, the lifts item. It is now the only in-scope
  failure family left from the fixture.
- The default-on setting was decided from these measurements. The airborne
  retry and the refusal logging were checked on 4 worlds only, not a full
  campaign.
- A stand-and-restart recovery costs its brake. A recovery that hands back
  moving onto the route would be faster; it is unmeasured.
- The onward hold after a kicked hop, from the fixture's open list, did not
  recur: a hit now ends the plan. It is not repaired for other causes.
