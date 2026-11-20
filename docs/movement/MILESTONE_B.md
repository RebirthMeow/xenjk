# Milestone B: a full 4v4 (plan, 2026-10-02)

**Status (2026-10-02):** B1a built and measured ([record](records/2026-10-02-camper-b1a.md)): a camper beside the
runner raises team captures 7.25 → 12.0 a world on Kejim and halves the runner's weapon deaths. Next in B1: a
demo-derived camper post, the hand-off, the camper's Force policy.
B1b (owner rules, camping around the stand) and the four agent items are merged ([record](records/2026-10-02-b1-merge-and-testing.md)):
keep-out on; displacement, camper protect and the hand-off measured and off.
B2 (owner, 2026-10-02): a true 2v2 is offense + chase each side; base support moves to B3. Two-sided 2v2 runs
([record](records/2026-10-02-b2-structure.md)): about 10 captures a world, standoff 46% of the time, red-blue balance inside
the spread.
B3: the 4v4 runs (blue 4.6, red 4.35 captures a world, standoff 24%); base support built and off (no capture gain)
([4v4](records/2026-10-02-b2-structure.md#the-4v4-b3-lineup-same-day), [base support](records/2026-10-02-base-support.md)).
4v4 stalls fixed and the idle runner camps the enemy stand (owner: denial more than capping), 2026-10-03
([record](records/2026-10-02-b3-stalls.md)); the runner's falls while camping are route exposure, +0.8 a world for both runners
([record](records/2026-10-03-runner-camp-falls.md)).
B3 on Dantooine v3 and scored (2026-10-03, [record](records/2026-10-03-b3-dantooine.md)): 9.1 captures a world against
Kejim's 7.6, falls a third of Kejim's, balanced; standoffs (both flags off their stands) 40-47% of the time, median
7 s, broken by a carrier's death and a third of breaks capped within 20 s; no frozen world in 64. Two chase D defects
fixed: it had run on default Force since 2026-09-28 (an LF `.jkb`: no saber, jump 1), and a brace on a carrier's
head froze a world. Frame cost: the eight bots miss the 25 ms frame in 5-8% of frames (p99 41-66 ms), the open B3
item; next: owner's call (HANDOFF).

Owner direction (2026-10-02): offense gets **camper** logic, base gets **support** logic, then a **full 4v4**. It takes
in milestone A's deferred team situations and replaces roadmap step 4 (chase and base milestones): the fixture defenders
already are the start of base D and chase D ([MILESTONE_A.md](MILESTONE_A.md)), so they are promoted, not rebuilt.

## Lineup

Each team: offense (the runner), camper (the second offense), base, chase. Both teams the same bots, so a red-blue
imbalance is a fault to find, not a result.

## What blocks a 4v4 today (structure, before behaviour)

- **One runner a world.** The runner is the bot named `Runner_v2` (`Bot2_IsMovementOnly`, ai_bot2.c); its harness,
  telemetry and Force policy assume one.
- **One defending team.** `DEFENDER_MAX 2` (bot2_defender.c): Defender_v2 in slot 0, Chaser_v2 in slot 1, and the
  teammate logic reads `defenders[1-slot]`.
- **Guests.** Every bot but the runner moves as a MoveGuest that writes nothing into the shared graph; eight bots on
  one graph need the same rule for every bot but per-team learned costs, or one owner of learning.
- **Frame cost.** Two searching bots ran p99 about 12 ms ([record](records/2026-09-28-strafing-chaser.md)); eight
  must stay inside the server frame. Measured before behaviour is built.

## Steps

Owner order (2026-10-02): 2v2 fixtures first, then the 4v4.

**B1. Camper, 2v2 one-sided** (runner + camper, blue, against base D + chase D, red; Kejim). The camper is a second
blue bot moving as a MoveGuest (as chase D does), so no second runner harness is needed yet. Situations from the pilot
([community record](records/2026-09-27-community-discussions.md#how-positions-play-decision-rules-stated)):
- while its carrier holds: stay within reach of the enemy stand to retake if the carrier dies (pilot: 34% retakes
  within 10 s from within 1,000 u against 29% farther);
- the camper's trade (situation 6): attack the enemy carrier with its own chase when the chase dies to the carrier and
  base (the enemy base is spent), else camp; in the one-sided 2v2 this is the camper fighting base D and chase D for
  its carrier;
- hand-off receiver (situation 3): the spent carrier self-kills when the camper is fresh and near;
- cut the enemy base off its carrier (push a respawning base away).
Arms: runner alone against runner + camper with the camper idle, then with each behaviour; laps worlds.

**B2. Two-sided 2v2 and base support** (offense + base each side, the classic 2v2). Needs one runner a team, so the
single-runner structure (above) is lifted here. Base support, with its own carrier holding:
- answer the carrier's `te` call (situation 1: the carrier at 0-20 Force runs to the base's reach and calls);
- the support loop (situation 4): TE, self-kill, respawn near the carrier, TE again; or clear the enemy camper; one
  plan, not both;
- retake a dropped own flag; never take the enemy flag except in an emergency.
Base loadout dark (TE, drain, grip); the runner learns to call and to come to its base. Red and blue must capture
within the spread of each other.

**B3. Full 4v4.** Offense, camper, base, chase a team; Kejim, then Dantooine v3. Scored on captures a team, hold
times, both flags held at once (the stalemate) and how it breaks, red against blue balance, frame cost, and the pilot's
human figures (carrier damage per minute, TE output, hold lengths) as reference rather than gates.

## Owner decisions (2026-10-02)

1. 2v2 fixtures first (B1 one-sided, B2 two-sided), then the 4v4.
2. Loadouts: the camper as the runner's (light: absorb, protect, push, pull, Speed); base dark (TE, drain, grip).
3. Acceptance: judged on the measured arms, as milestone A was.
4. Frame budget: not set; each step reports p99 frame cost for the owner to judge.
