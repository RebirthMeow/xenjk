# Runner absorb at pit edges, first measurements (2026-10-02)

Plan: [MILESTONE_A.md](../MILESTONE_A.md) steps 0-1. Step 0 (loadout absorb 3 + protect 3, 1d29e520): Kejim runner
floor 24/24 at classes 150 and 50 in both arms and **48 of 48 completion times identical** with and without the new
powers (`.codex-research/step0-20261002-b/`, old runner file swapped in for the control on the same build). The bench
now sets `bot_runnerCapKill 0` (e89bbe68): with the play default on, the first identity run counted the self-kill at
each capture as a death after the objective (`.codex-research/step0-20261002-a/`, 9 return-leg failures).

Step 1, `bot_runnerAbsorb` (branch `movement/runner-absorb`, not merged): perceived enemies within 1,024 u, a soft Force
estimate per enemy life (50, -20 a seen throw, regeneration), exposure as a pull or push applied now and swept under
gravity into a lethal fall or hurt volume, activation only. 16 laps worlds an arm on the current defender defaults
(`.codex-research/absorb-20261002-a/`, `summ.py`):

| arm | captures (se) | runner deaths | falling | pulls hit | throw → pit | presses | absorb up | Speed while carrying |
|---|---|---|---|---|---|---|---|---|
| off (ab0) | 4.50 (0.63) | 39.5 | 2.12 | 12.8 | 0.62 | 0 | 0% | 95% |
| v1 (ab1, b4443012) | 1.38 (0.33) | 38.6 | 1.44 | 8.8 | 0.12 | 35.9 | 34% | 70% |
| v3 (ab3, 31aeecb3) | 0.81 (0.23) | 42.7 | 2.06 | 7.8 | 0.12 | 37.7 | 30% | 83% |

v1 had two bugs: the threat estimate never regenerated (per-frame integer division) and the reserve defaulted to 0.
v3 fixes both and adds a Speed-first gate (absorb only if the Force projected to Speed's expiry still buys the next
Speed). Absorb does what it should against throws (throw → pit 0.62 → 0.12, pulls landing 12.8 → 7.8), and it costs
the run: pickups 14.0 → 10.0 a world, captures per pickup 32% → 8%, the median carry before death 2.8 → 1.2 s,
carrier speed median 498 → 425 u/s (p25 393 → 254).

**Why.** Activation only keeps every press up for its full 20 s: about 10 + 33 Force per press, 37 presses a world,
absorb up 30% of the time. The runner's movement runs on Force (Speed 50, force jumps), and the Speed gate alone does
not protect the rest. Exposure is also loose: 37 presses against about 13 pulls that land in the control.

**Not adopted.** Next candidates: lower absorb once nothing has been exposed for a short time after its 1,500 ms
minimum (owner's call: it reverses activation only); a tighter exposure test (the committed path, pull reach); and
gating on the whole Force the plan needs, not only Speed.

## The cause, the forecast fix, and the owner's rule (same day)

**Absorb froze the runner's searches.** The movement forecast refused every rollout while a power other than Speed or
levitation was active (`UnsupportedPowers`, bot2_forecast.c), so for the 20 s absorb was up every hop candidate was
refused and every planned hop refused at takeoff (101 a world against 16). Force was not the main term: without
absorb the runner buys Speed once a life (a second in 10% of lives), never force-jumps, and sits near 50 Force.
947c39e4 lets the rollout carry absorb and protect and runs their drain (1 per 600 / 300 ms) as `WP_ForcePowerRun`
does. Only states with one of them active are affected, which the runner had never been in, so nothing learned
changes and no re-exploration is needed (owner: no proof run).

| arm (16 laps worlds) | captures (se) | runner deaths | throw → pit | carrier pulls with absorb up | carrying without Speed |
|---|---|---|---|---|---|
| off (ab0) | 4.50 (0.63) | 39.5 | 0.62 | 0 of 12.2 | 5% |
| mode 1 + forecast fix (ab4, 947c39e4) | 2.94 (0.27) | 34.5 | 0.06 | 8.5 of 9.6 | 23% |
| mode 1 + "approach only" (ab5, bb3592f8; a misreading) | 3.19 (0.33) | 39.4 | 0.81 | - | 16% |
| mode 2, owner's rule (ab6, eb16c132) | 3.31 (0.39) | 39.1 | 0.56 | 8.8 of 10.7 | 19% |

- **Throws land on the carrier:** 12.2 of 12.8 pulls a world in the control.
- **The second Speed is the carrier's:** without absorb a fifth of carrying time runs on a second Speed purchase;
  any absorb on the carry takes the Force for it (no regeneration while a power is up), hence carrying without
  Speed 5% → 16-23%.
- **Mode 2** (raise within 1,500 u of the flag it goes for, drop over 50 Force) has absorb up for most carrier pulls
  but did not lower deaths; the drop fires about 5 times a world. Its extra falls (3.06 against 2.12) are
  unexplained at 16 worlds; a correlation with presses was confounded (presses from the bottom of the pit, where a
  press could not raise absorb and was repeated every frame, now limited to one per 300 ms).

**Owner decision (2026-10-02): accept the trade.** `bot_runnerAbsorb` defaults to 1 (8d0e58d1, merged 8fd60ddf):
about 5 fewer runner deaths and 90% fewer throw pits a world for about 1.5 fewer captures. Next: protect (step 2).
