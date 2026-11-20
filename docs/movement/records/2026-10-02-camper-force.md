# Milestone B: the camper's Force policy, protect (2026-10-02)

Plan: [MILESTONE_B.md](../MILESTONE_B.md), open item of [B1a](2026-10-02-camper-b1a.md) ("the camper never raises
protect"). Build `camperforce-build-20261002-a`, `.codex-research/camperforce-20261002-a/` (`run_cp.sh`,
`camper_force.py`). The control is `camper-20261002-b/ce` (build `camper-build-20261002-e`, this branch's base).

## What was built

- **`bot_camperProtect` (default 0)**: the camper uses the runner's protect decision, the milestone A rules
  ([protect v1](2026-10-02-runner-protect-v1.md)): reactive only (a seen enemy missile closing in within 600 u,
  ranged damage taken in the last 300 ms, a seen enemy mine in splash reach), activation only, never pressed while up
  (a second press switches it off), at most one press per 300 ms, at least 10 Force. It presses only when the frame's
  one `generic_cmd` is free, so the movement's Speed purchase and Engage's throw come first. Absorb stays off.
- **One decision, not a copy**: `RunnerProtectPolicy` is split into the cvar read and `ProtectDecide`, which takes
  the client; the new `Bot2_ProtectCommand(client, cmd)` (ai_bot2_objective.c) runs that client's own perception
  (`RunnerPerceive`: threat model, own damage, mine memory) and then the decision. bot2_defender.c calls it at the end
  of the camper's command, after `CamperMove` and `Engage`.
- **Per-client state**: every table the decision reads was already keyed by client (`runnerThreat[observer][enemy]`,
  `runnerDamage`, `runnerMineSeen`, `runnerProtect`, `runnerAbsorb`); nothing assumed a single runner. The camper's
  row is cleared on each dead frame (`Bot2_RunnerForceReset`, as the runner's is through `Bot2_ClearState`) and at
  the release. The absorb deferral reads `runnerAbsorb[camper].exposed`, which stays 0 (absorb never evaluates for
  the camper).
- **Knowledge boundary holds**: missiles and mines must be seen (`RunnerCanSee`), damage is the camper's own health
  drop and its own `lasthurt_*`, enemies' positions are where the camper last saw them; no live state of another
  client is read unseen.
- **Logs**: the camper's lines print as `[DEFENDER] CAMPER_PROTECT`, `CAMPER_PROTECT_SKIP` and
  `CAMPER_THREAT_FORCE` (`ForceLogTag`), so `[RUNNER] PROTECT` counts stay the runner's; the runner's output is
  unchanged.
- **Movement forecast**: protect is in the forecast's allowed set for every `modelForce` rollout, and the guest's
  rollouts use the same `RolloutRequest` (`modelForce` set). Measured: the camper launched 33.8 hops a world with
  protect up, and no rollout in the arm carried `BOT2_FORECAST_ACTIVE_POWER`. Runner movement code is untouched, so
  the bench floor is not affected.

## Results (8 laps worlds of ten game minutes an arm, Kejim, B1b defaults)

| | control (`ce`) | camper protect (`cp`) |
|---|---|---|
| runner-team captures (se) | 10.75 (1.29) | 10.12 (1.11) |
| runner / camper captures | 7.38 / 3.38 | 6.50 / 3.62 |
| camper protect presses | 0 | 18.4: missile 14.4, damage 3.2, mine 0.8 |
| camper protect skipped for Force | - | 16.4 |
| camper protect up | 0% | 15.6% |
| camper Speed up / mean Force | 74% / 49 | 65% / 38 |
| camper deaths (se) | 18.4 (2.2) | 14.4 (1.0) |
| camper weapon deaths (se) | 14.0 (1.7) | 10.5 (0.9) |
| camper falls | 4.4 | 3.9 |
| defenders killed by the camper | 9.5 (base D 6.1, chase D 3.4) | 8.9 (5.6, 3.2) |
| camper hop launches (protect up) | 273 (0) | 278 (34) |
| runner protect presses / up | 21.2 / 15% | 20.2 / 12% |

Every world distinct, no crashes.

- **Camper deaths fall by a quarter** (weapon deaths 14.0 to 10.5, about 1.8 standard errors), the same direction as
  the runner's protect result. Kills and captures do not move; the camper's Speed uptime drops 74% to 65% (no Force
  regeneration while protect is up). The camper throws about once a world in either arm, so starved throws cost
  nothing.
- **Not factor-sized**: the capture difference is inside the noise and the death drop under two standard errors.

**Verdict: inconclusive at 8 worlds; default 0 (off)** per the brief. Turning it on costs nothing measurable and
probably saves about 4 camper deaths a world; an owner decision or a second 8-world arm would settle the default.

## Open

- A press is mostly spent: 16.4 skips a world for Force (0-3 Force after a protect expires with Speed bought). A
  Force reserve for Speed, as absorb has (`bot_runnerAbsorbReserve`), is not built for protect.
