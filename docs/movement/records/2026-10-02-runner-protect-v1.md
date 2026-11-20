# Runner protect, first measurement (2026-10-02)

Plan: [MILESTONE_A.md](../MILESTONE_A.md) step 2. `bot_runnerProtect 1` (branch `movement/runner-protect`, 46f194ab, not
merged, default 0): raise protect under perceived fire (a visible enemy missile within 600 u closing in, damage from
an armed enemy within 1,200 u in the last 300 ms, or splash reach of a trip mine it has seen), only when absorb is not
up and absorb's pit exposure is not set; at least 10 Force; activation only, one press per 300 ms.

16 laps worlds an arm, current defaults with absorb mode 1 on in both (`.codex-research/protect-20261002-a/`):

| arm | captures (se) | runner deaths | rocket | trip mine | flechette alt splash | protect presses | protect up | deferred to absorb |
|---|---|---|---|---|---|---|---|---|
| off | 3.25 (0.44) | 38.9 | 4.2 | 7.0 | 5.5 | 0 | 0% | - |
| on | 3.69 (0.34) | 39.1 | 2.4 | 5.4 | 7.1 | 10.8 (missile 8.2, damage 2.6) | 4% | 54.9 |

- **Protect is rarely up (4%)**: absorb holds the one slot the two powers share (up 33-36% of the time) and protect
  deferred to it 55 times a world. Deaths are unchanged; the per-cause shifts are inside the spread.
- **Its triggers are late**: a missile seen closing in, or damage already taken, mostly comes in the same burst that
  kills (the agent's hand check: a press 375 ms before a five-hit burst that killed anyway).
- Weapons cause 94% of runner deaths and throw pits about 0.3-0.6 a world, yet absorb holds the slot. Absorb's
  exposure test is broad (any perceived enemy able to throw within 1,024 u, a gravity-only sweep with no recovery):
  most of its claims are probably not real pit danger.

Not adopted. Next candidates (owner's call): tighten absorb's exposure (real pit edges, the enemy close enough to
pull, the runner's own recovery counted) so the slot is free for protect; raise protect pre-emptively for a carrier
in a fight (enemies with ranged weapons in view) rather than after the first hit.

## Absorb off, protect on (owner, same day)

Same build, 16 laps worlds an arm, the four combinations (`.codex-research/protect-20261002-a/`, `run.sh`, `run2.sh`):

| arm | captures (se) | runner deaths | weapon deaths | runner self-kills | throw → pit | protect up | absorb up | carrying without Speed |
|---|---|---|---|---|---|---|---|---|
| both off (nn) | 4.31 (0.53) | 40.4 | 33.3 | 4.3 | 0.06 | 0% | 0% | 4% |
| **protect only (po)** | **6.44 (0.47)** | **29.8** | **20.8** | 6.4 | 0.94 | 23% | 0% | 35% |
| absorb only (pr0, today's default) | 3.25 (0.44) | 38.9 | 33.4 | 3.2 | 0.31 | 0% | 36% | 20% |
| both (pr1) | 3.69 (0.34) | 39.1 | 33.8 | 3.7 | 0.38 | 4% | 33% | 24% |

- **Protect alone wins on both counts:** captures +2.1 a world over both off (about three standard errors) and
  weapon deaths 33.3 → 20.8. Its presses come from missiles seen closing in (24.9 a world) and damage (8.8); it is up
  23% of the time. The extra self-kills are the post-capture self-kill on the extra captures.
- **It costs Speed and pits and still wins:** carrying without Speed 4% → 35%, throw pits 0.06 → 0.94, and the
  defenders land more pulls (11.7 → 28.5) on a runner that lives longer. Surviving the weapons is worth more.
- **Absorb is the worse use of the slot** in every combination: absorb only and both run 3.3-3.7 captures. Absorb
  being up blocks protect, and its exposure test claims the slot a third of the time.

**Owner decision (2026-10-02): protect on, absorb off by default.** Merged (407e7827); defaults swapped in
`ai_bot2_objective.c` (`bot_runnerProtect` 1, `bot_runnerAbsorb` 0), build `protectdef-build-20261002-a`; one
300 s laps world with neither cvar set: 17 protect presses, 0 absorb presses, 3 post-capture self-kills.
