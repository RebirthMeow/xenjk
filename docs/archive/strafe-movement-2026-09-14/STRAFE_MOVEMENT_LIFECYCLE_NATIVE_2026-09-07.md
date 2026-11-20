# Native lifecycle, contention and recovery of the flight adapter (R4 / K06), 2026-09-07

Executes R4 of the [runtime review](STRAFE_MOVEMENT_RUNTIME_REVIEW_2026-09-07.md)
against the production flight adapter on Kejim. Before this campaign every
one of the 175 native releases recorded by K05 (development and holdout) was
`complete`: no refusal after entry, no recovery, no gate, publication, disable
or ownership release had a native example, and no preparation had ever been
deferred. This protocol constructs each of those paths with one declared
intervention per run, records what the adapter did, and measures the one-slot
preparation schedule under genuine same-frame contention.

All evidence is local under `.codex-research/`; nothing was installed or
promoted and both flight controls stay off by default.

## What changed in the adapter (contracts now in place)

`codemp/game/ai_bot2_flight.c`, mocked by `tools/strafe_flight_adapter_check.py`
(74 scenarios), native build `strafe-build-r4-lifecycle-65dba03922fd42d6a80304a379cd8d5b`:

- **Deferred-first admission.** A client deferred on one frame holds a ticket
  for the next slot, in deferral order; a newcomer that Pmove visits first on
  the next frame, however low its index, is deferred behind it. A ticket whose
  holder stops asking expires after two frames (`FLIGHT_TICKET_FRAMES`), so an
  absent client cannot hold the slot. Every `prepare_deferred` prints
  `queue= head= waited=`, every `prepare_cost` prints `waited=`, and
  `tools/strafe_flight_log.py` reports admissions and deferrals per client, the
  longest ticket wait, the longest run of consecutive deferrals and the deepest
  queue.
- **Explicit owner-lost release.** At the Pmove boundary, a prepared, active or
  recovering client whose supported plan did not offer the current frame is
  released as `owner_lost`, with phase, ground, health and vertical speed
  printed. The owner has already taken the client back; state that outlives
  its owner no longer sits silently until an unrelated plan calls.
- **Airborne handoff goes through recovery.** A publication, artifact or
  force-gate release that finds an active flight in the air between launch and
  landing enters the same bounded `FLIGHT_RECOVER` as a kernel refusal, with
  `cause=` recorded (`refused`, `publication`, `artifact`, `gate`). Recovery is
  judged only by a reduced gate (living client, normal physics): another
  republication or an active force does not end it, being grounded does. A
  dead client, abnormal physics, an operator `disabled`, and a client the
  flight has not launched yet still release at once.
- **Best-effort recovery, reported.** When the projected touchdown cannot be
  steered into the range but the client is still above it,
  `Bot2_FlightRecoverCommand` steers toward the nearest point of the range and
  returns 2; the adapter prints `recover_best_effort` once with the range
  geometry. Below the range it refuses, and the adapter prints
  `recover_refused` with along/lateral/height so a doomed fall is attributed,
  not guessed at.
- **No state across lives.** `Bot2_ClearState` resets the adapter
  (`external_reset`), so a native death, respawn or bot setup never inherits a
  prepared or active flight.
- **Probe for fixtures.** `Bot2_FlightAdapterProbe` exposes state, phase,
  window and the middle of the range ahead.

Research fixtures: `bot2_flightcase.c` (one declared intervention per solo
run, keyed to the probe), `bot2_movecost.c` `bot_moveCostHold` (declared
per-client start holds) and `bot_moveCostSameSpawn` (clustered start, refused
by measurement, retained).

## Lifecycle campaign

Frozen plan and inventory: `tools/strafe_flight_lifecycle_campaign.py`;
audit: `tools/strafe_flight_lifecycle_report.py`
(`tools/test_strafe_flight_lifecycle_report.py`, 12 negatives/positives).
Eighteen solo 120-game-second accelerated production runs, adapter armed with
the `…-r3-` artifact and the scoped guard, runner phase 2 (unused by K05),
nine declared cases per team: a no-intervention control, an 80 unit/s lateral
impulse in the first flight (phase 2) and in the second (phase 5), a 200
unit/s impulse declared as a boundary counterexample, a player-sized body on
the middle of the first landing range, a one-second operator disable, a Force
Speed input in the air, lethal damage while carrying the flag, and a supported
ownership reset mid-flight.

Collections: `strafe-flight-lifecycle-r4-20260907` (build
`…-0b8e6bce…`, before the probe's landing output;
its body cases could not place a blocker over the gap and its first audit
carried an auditor defect - both retained),
`…-v2` (final build; audited under its frozen gate, two cases failed the
literal 3 s reaction wording and a log-only continuity reading, retained as
`-v2-report-v1` and the clarified gate was refrozen), and **`…-v3`** (final
build, clarified gate, the evidence below, audit `…-v3-report/`).

Result on v3, all eighteen inputs valid, every intervention fired at its
declared entry and phase, no preparation deferred, no artifact refused:

| Case | Fired at (adapter phase) | Adapter path after the fire | Ground / continuity | Deaths |
|---|---|---|---|---|
| control (red, blue) | - | 15 entered, 15 complete each | - | 0 |
| impulse 80, first flight (red, blue) | phase 2, ~10.0 s | flight continued, grounded refusal at +1.6 s (`refused`, ground), follower handed back on the ground | grounded +0.8 s, moving on the plan +1.5 s | 0 |
| impulse 80, second flight (red, blue) | phase 5, ~10.4-10.9 s | flight absorbed the kick and completed (`exit resumed=1`) at +1.2-1.5 s | +1.2-1.5 s (log) | 0 |
| impulse 200, boundary (red) | phase 2, 10.2 s | kernel refusal at +0.8 s -> `recover` -> `recover_refused` at once: height -7.6 (below the range), lateral 99.8 | grounded on a lower surface +2.5 s, moving +6.1 s | 1 (pit, +5.1 s) |
| impulse 200, boundary (blue) | phase 2, 9.75 s | same shape: height -7.6, lateral 88.1 | grounded +1.5 s, moving +5.8 s | 0 |
| body on the landing (red) | phase 2, 10.0 s | 29 body contacts, the flight still completed at +3.35 s | +0.8 s / moving +2.5 s | 0 |
| body on the landing (blue) | phase 2, 9.55 s | 11 contacts, kernel refusal at +1.5 s -> `recover` -> one `recover_best_effort` -> `recovered` on the ground at +1.6 s | +0.8 s / moving +3.4 s | 0 |
| operator disable 1 s (red, blue) | phase 2 | `disabled` at +25 ms, airborne, explicit; re-enabled at +1 s; the follower landed the client and kept its plan; later windows entered and completed | +0.8 s / moving +2.1-3.1 s | 0 |
| Force Speed input (red, blue) | phase 2, entry 3, ~18.6-19.6 s | force gate fails at +25 ms -> `recover cause=gate` -> `recovered` on the ground at +0.8 s | +0.8 s / plan +2.15 s (log) | 0 |
| lethal damage while carrying (red, blue) | phase 2, return leg, ~34-36 s | `external_reset` at +25 ms on the death frame (the only reset), native respawn, flag dropped natively, plan re-acquired +2.4-2.9 s | respawn +1.2 s | 1 (declared) |
| ownership reset (red, blue) | phase 2, ~9.5-10.0 s | `owner_lost` at the next boundary (+25 ms, airborne, explicit); new plan +1.9 s | +0.8 s | 0 |

Predicates per case are in the audit (`fired`, `reacted_within_bound`,
`no_airborne_handback`, `no_death_added`, `grounded_within_bound`,
`plan_within_bound`, `hidden_reset`, and for boundary cases
`boundary_refusals_explained` with deaths reported). `k06_lifecycle_pass` is
true: 14 pass, 2 boundary pass, 2 controls pass, none inconclusive.

What the measurements say, and their limits:

- The airborne hand-back that killed the bot in the review's captures no
  longer occurs on any exercised path: every airborne release is one of the
  declared exceptions (`disabled`, `owner_lost`, `external_reset` on a death
  frame, `recover_refused` with geometry below the range).
- A doomed fall is still a doomed fall. The 200 unit/s kick puts the client
  7.6 units below the landing surface by the time the kernel refuses; nothing
  above the range is left to aim at and red died in the pit. It is retained as
  the boundary, explained by geometry, and reported - not hidden, not excused.
- The blue body case is the one native example of the best-effort steer; it
  landed. One example is an existence proof, not a rate.
- The body did not always disturb the flight (red completed through 29
  contacts). Whether a body ends a flight depends on where in the range the
  client lands; the fixture places it at the range middle.
- The Speed input succeeded at entry 3 with force 20-23; the case shows the
  force-gate path, not resource policy.
- Plan continuity on an unchanged plan is measured from the capture (300
  units of travel after regaining the ground), because the follower prints
  nothing per frame; the audit says which evidence it used.
- Publication change mid-flight was not constructed natively (no plan
  republishes during a flight in these worlds); it is covered by the mocked
  scenarios (airborne publication -> recovery) and by the native reset case's
  `owner_lost`.
- These are accelerated solo worlds: outcome evidence only, no timing claim,
  not K07.

## Contention and fairness (eight bots)

Native same-frame contention was **not obtained**. The one-slot schedule has
still never deferred a native preparation; the deferred-first ticket contract
is established by the mocked harness (a deferred client admitted next frame
with `waited=25` while a lower-numbered newcomer is deferred behind it, and a
two-frame ticket expiry) and by the new telemetry, not by a native frame.
Every construction was retained, with what it showed:

| Arm (`.codex-research/strafe-flight-cost-r4-20260907/`) | What it constructed | Result |
|---|---|---|
| `baseline/` (8 bots, build `…-65dba039…`) | Distinct-spawn workload; reproduces the R3 event timeline exactly (21100/22500/27325/27650 ms…) | 8 preparations, busiest frame 1, no deferral; movement p95 0.284 / **max 5.112 ms** (the same six-forecast preparation R3 measured at 3.72 ms; 4.97 ms of it forecast time) - fails the 5 ms maximum on this sample |
| `contention-hold/` (8 bots, holds 6:1400 4:2425) | Two clients time-shifted onto the baseline's contended frames | Every timeline changed, including unheld blue clients: eight clients couple through flag state; 7 preparations, no deferral |
| smoke `strafe-cost-contention-smoke-20260907/same-spawn-{1,3}` (+ traced) | A team clustered side by side at one spawn | Zero preparations: clustered starts reject supported preparation (46 `prepare_reject`, 6 plans in 20 s); refused construction |
| `pair-baseline{,-s1,-s2,-s4,-s5,-s6}/` (2 bots, one per team, build `…-7c6c754a…`) | Decoupled world per spawn index | Preparations only from spawn indices 2-4 (0-based); no red/blue pair differs by a multiple of 200 ms within one index |
| `pair-hold-500/` | Red held 500 ms | Blue unchanged (decoupled), red drifted 125 ms and its window outcome changed: a hold that is not a multiple of 200 ms shifts the 8 ms Pmove phase against the 25 ms frame |
| `pair-hold-r4b3-3400/`, `pair-hold-r3b5-3600/` (build `…-a5a81bcd…`, per-team spawn; `pair-hold-500/` on `…-7c6c754a…`) | Cross-index pairs whose baseline gap was a 200 ms multiple | Both timelines changed with the partner's spawn and the run's start time (6600/5200 vs 5600 ms): the two-client world is not a pure time shift either |
| `sixteen/` (16 bots) | Chance-based density | 8 preparations, busiest frame 1, no deferral; movement p95 0.507 / max 4.612 ms, server frame max 5.36 ms, all sixteen clients alive on all 800 frames |
| `baseline-b7/`, `control-b7/` (8 bots, final build) | Second cost sample, matched control | Adapter p95 0.369 / max 3.718 ms, `eight_bot_budget_pass`; control max 0.748 ms; all eight alive on all frames |

Conclusion: serialisation holds on every native frame ever measured; fairness
and starvation bounds are mocked and instrumented, and the first native
deferral will be reported by `prepare_deferred … queue= head= waited=` and the
cost report's `contention` block whenever it occurs. Constructing it
deterministically needs a fixture that pauses the world rather than one
client, which is a K07 stress item, not a lifecycle result. The 5.112 ms
sample is retained as a budget failure on that run: the same preparation
measured 3.72 ms (R3) and 3.718 ms (`baseline-b7`) with identical rollout
counts, so it is per-run variance of the same work, and the budget claim
stands on the two passing samples only with that variance stated.

## Reproduction

```bash
python tools/strafe_flight_lifecycle_campaign.py --action freeze --build .codex-research/strafe-build-r4-lifecycle-65dba03922fd42d6a80304a379cd8d5b --artifact .codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt --out .codex-research/<fresh> --first-port <port> --phase 2
python tools/strafe_flight_lifecycle_campaign.py --action run --out .codex-research/<fresh>
python tools/strafe_flight_lifecycle_report.py --campaign .codex-research/<fresh> --out .codex-research/<fresh>-report
```

```bash
python tools/strafe_cost_runner.py --build .codex-research/strafe-build-r4-lifecycle-65dba03922fd42d6a80304a379cd8d5b --assets "game_directory/Jedi Academy/GameData" --catalog tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt --bots 8 --supported-hop 2 --supported-corners 2 --flight-artifact .codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt --out .codex-research/<fresh> --port <port> --flight-adapter --forecast-guard --hold "6:1400 4:2425"
python tools/strafe_cost_report.py --run .codex-research/<fresh> --out .codex-research/<fresh>-audit
```

Mocked checks after any C change: `tools/strafe_flight_adapter_check.py`,
`tools/strafe_forecast_guard_check.py`, `tools/strafe_movecontrol_check.py`,
`tools/strafe_flight_check.py`; Python: `tools/test_strafe_flight_lifecycle_report.py`,
`test_strafe_continuous_runner.py`, `test_strafe_cost_runner.py`,
`test_strafe_cost_report.py`, `strafe_flight_log.py`.
