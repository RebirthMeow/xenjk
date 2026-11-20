# Parity compares nothing because the walk it forecasts ends at the flag

Recorded 2026-09-20 under the owner's direction to restore meaningful
forecast/execution parity comparisons. **No source change and no rebuild**:
two cvar-only diagnostic worlds on the frozen build
`.codex-research/select-override-build-20260920-a`, plus read-only counting
over retained worlds and a reading of the sources as they stand. Protocol and
evidence in `.codex-research/parity-triggers-20260920-a/`.

`compared: 0` has been reported beside every result for weeks, and every
record has had to say that zero mismatches therefore qualifies nothing. This
is why.

## Every parity build is refused, and always for the same reason

Over the 36 accepted worlds of
[the ablation](2026-09-20-preference-decisive-commit-priced.md): **every**
parity event in every world is `forecast_refused`, 5,394 of them, and every
one carries `unsupported=256` - `BOT2_FORECAST_TRIGGER` - with
`restored=1 animation_restored=1 world_changed=0`. Not one `boundary_time`,
`before_state`, `command`, `executor_state`, `after_state`, `live_declined`
or `forecast_declined` event exists, because none can: the harness never arms.

The refused rollouts are not empty. Their `frames=` runs from 1 to **244**,
and inside one world the values sweep the whole range consecutively - the same
contact seen from roots one frame further back each time. `ParityBuild`
discards the build on any `unsupported` bit, so up to six seconds of predicted
frames are thrown away per build.

## The trigger is the objective

`bot_forecastTriggerLog=1` names the entity behind each refusal. In the
diagnostic world, of 556 trigger refusals across all rollouts:

| Entity | Refusals |
| --- | ---: |
| `trigger_hurt` (48) | 304 |
| `team_CTF_blueflag` (83) | 139 |
| `team_CTF_redflag` (82) | 53 |
| `item_medpak_instant` (55, 53) | 59 |
| `trigger_push` (40) | 1 |

Matching each **parity** refusal to the trigger line that produced it, by
frame index: **119 of 119 are the flag** - 110 blueflag and 9 redflag, every
frame index agreeing. The `trigger_hurt` and medpak contacts refuse search
rollouts, not parity builds.

That is the whole mechanism. The parity harness forecasts the walk the
executor is running; the walk is heading for the flag; the flag's pickup box
is a trigger; the rollout is refused the moment it reaches it. The better the
forecast horizon, the more certainly it ends on the objective.

`bot_forecastItems=1`, which already exists and excludes weapon, ammo, armor
and health pickups, removes the medpak refusals and **changes nothing for
parity**: 119 refusals, 0 compared, and the same 17,575 ms completion.
`trigger_hurt` refusals rise from 304 to 334, because rollouts that used to
stop at a medpak now run on to the pit.

## The second defect, from the sources

Arming on the prefix is necessary but not sufficient. `RolloutRequest` sets
`request->frames = parity.frames` for **every** rollout in the movement
sources, so each candidate the straight search rolls out writes over the same
buffer the parity build filled (`bot2_move.c:1367`). The comparison in
`Bot2_MoveParityBeforePmove` runs before that frame's searches, but the
`after_state` comparison in `Bot2_MoveParityAfterPmove` and every later
frame's comparison read `parity.frames[parity.index]` after the searches have
overwritten it.

This is latent today: nothing arms, so nothing is corrupted. It would bite the
moment the first defect is fixed, and it would bite as a **mismatch**, which
is exactly the signal a repaired harness exists to produce.

So a repair is two changes, in this order: give the armed prediction its own
frames, then accept a rollout that stopped only on a trigger contact and
compare the frames before it. The contact frame itself is comparable -
`G_TouchTriggers` runs after the Pmove boundary the harness compares at
(`g_active.c:3382`) - but nothing after it is, so the build must end there.

## What this does not establish

- No repair is implemented and nothing is installed. This names the cause.
- The 5,394 refusals are from one case on one map. Other cases may refuse for
  other reasons; only `red_out_L1_stand` was counted here.
- The buffer-sharing defect is read from the sources, not measured: it cannot
  be measured while the harness never arms.
- The two diagnostic worlds have unpinned starts. Their completion times are
  not comparable arms and no treatment effect is claimed from them.
- Whether a repaired harness reports **zero** mismatches is unknown, and it is
  the point of repairing it. Mismatches found after the repair are a result,
  not a regression of it.

## Reproduce

One diagnostic world, from the workspace root, on an unused port:

```powershell
python tools/movement_bench.py `
  --build .codex-research/select-override-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 55700 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1 `
  --set bot_forecastTriggerLog=1
```

Then count `[FORECAST] trigger` lines by classname, and match each
`event=parity reason=forecast_refused` to the trigger line before it by frame
index.
