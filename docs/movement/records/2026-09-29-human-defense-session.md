# Human defense session against the runner, 2026-09-29

The owner played a 1v1 on Kejim against `Runner_v2` to show how a player
defends. The session is compared here with the pug base study
([2026-09-28-base-defender-study.md](2026-09-28-base-defender-study.md)),
with our defenders ([base and chase D](2026-09-29-base-chase-defenders.md)),
and with a public strategy guide. It is one session by one player, so the
record describes behaviour and does not set parameters.

## Session

- `tools/play_vs_runner.py --build .codex-research/roles-build-20260929-c`,
  `mp/ctf_kejim`, localhost. The owner was red (`Padawan`) and the runner blue,
  in the gates' production configuration. The build's 1,133 source hashes
  equal the tree at 86a32b40.
- Retained in `.codex-research/play-vs-runner-20260929T205057Z/live/`: server
  demos from both POVs, the server console, the runner's play log and
  `play-game.log`.
- `python tools/play_defense_study.py .codex-research/play-vs-runner-20260929T205057Z`
  writes `report.txt` and `summary.json` there.
  - **Sources:** the owner's POV demo through `base_defender_study.extract_demo`
    (217 s, 4,389 samples, no lost events); the runner's play log (250 ms
    samples); the game log (item pickups and kills, 1 s resolution).
  - **Inferred, not observed:** the action behind a kill, taken as the POV's
    last fire or hit within 4 s or its last push or pull within 8 s, since a
    fall takes several seconds; and the range of a fire.

## Outcomes

The runner made 5 pickups and scored once. That capture came while the owner
was still setting up: the flag was taken at 21.7 s with the owner 336 u from
the stand, typing. The owner killed the runner 14 times and returned the flag
twice. All 8 of the owner's deaths were `/kill`. The last pickup, at 234.7 s,
was cut off by the shutdown.

| Runner death | n | Carrying | Preceding owner action |
| --- | --- | --- | --- |
| Pit (`MOD_FALLING`) | 6 | 1 | push 4.4-4.7 s before (4); pull 4.6 and 6.5 s before (2) |
| Repeater alt, splash or direct | 4 | 1 | fire 25-300 ms before; one pull 5.9 s before |
| Trip mine splash | 2 | 0 | mines laid on the stand |
| Rocket, splash or direct | 2 | 1 | fire 25-450 ms before; the carrier kill 2.0 s after a pull |

- **Mostly before the pickup.** 11 of the 14 deaths came before the pickup.
  Four of them were within 600 u of the owner's stand, two of those by the
  stand's own trip mines. Only 3 carriers were killed, and all 3 had been
  pulled 2.0-5.9 s before.
- **Pit kills are pushes and pulls on a hopping runner.** In four of the six
  falls the runner was at full health (102-107). Every fall followed a push or
  pull in the 4.4-6.5 s before it, and nothing else in the 4 s before. 12 of
  the 15 Force throws were followed by a runner death within 8 s.
  - **Play-log traces** of the throws at 88.0, 151.4, 199.2, 206.8 and
    220.8 s: at each throw the runner was airborne over the chasm, with no
    ground under it.
    - The throw cut its horizontal speed to 1-5 u/s at 151.4 and 199.2 s,
      and to 3 u/s at 88.0 s.
    - It reached the pit floor (z = 32) about 2.3 s later.
    - It stood there alive for another 2.2-2.5 s until the pit killed it.

    So the throw kills at once, and the logged gap is the fall plus the
    pit's delay.
  - **The pilot's throws.** In the
    [one-defender pilot](2026-09-27-one-defender.md), throws from the stand
    post almost never killed: 2 of 31 native hits became pit deaths. The
    runner steered onto ledges 20-50 u off the passive fall line. The owner
    threw in mid, while the runner was in the air over the gap, and it could
    not recover.
- **The same spot twice.** Two falls, at 203.9 and 211.5 s, happened at the
  same pit, around (2330, 2970). The runner took the same route into the same
  push both times.

## Position

The owner's position shifted during the match (median distance per 15 s
window, owner alive):

| Time | To own stand | To enemy stand |
| --- | --- | --- |
| 11-30 s (setting up) | 336 | 6,611 |
| 30-150 s | 408-2,770, med ~1,780 | 4,191-7,085 |
| 150-195 s | 1,440-2,538 | 4,382-5,453 |
| 195-228 s | 3,168-4,178 | 2,695-3,705 |

- **Forward, not on the stand.** With the own flag home, the owner's distance
  from the stand was:
  - p25 / median / p75: 1,063 / 1,917 / 2,757 u;
  - within 512 / 1,000 / 1,500 u: 12 / 24 / 36% of the time.

  4v4 pug base players on Kejim sit much closer: a median of 797 u, 58% of the
  time within 1,000 u. The owner was 64 u or more below the stand's height 67%
  of the time (pugs 49%).
- **Posts.** Within 1,500 u of the stand, weighted k-means with k = 5 gives:

  | Post (x, y, z) | Share | From the stand | Pug study equivalent |
  | --- | --- | --- | --- |
  | (98, -937, 1930) | 34% | 229 u | the pug top red post (195, -952, 1926) |
  | (545, 35, 1763) | 21% | 1,258 u | none |
  | (200, -356, 1844) | 19% | 805 u | none |
  | (-398, -1233, 1772) | 17% | 555 u | near the pug post (-256, -1343, 1861) |
  | (683, -1193, 1803) | 10% | 532 u | the pug post (634, -1235, 1810) |

- **The runner is met forward.** The runner came within 1,500 u of the stand
  12 times. The owner was alive every time but within 600 u of the stand only
  twice, at a median of 349 u from the runner.
- **Late phase.** From about 195 s the owner played on the runner's outbound
  route near the enemy base and pushed it into pits there. This is chase
  defence played before the steal.

## Weapons and items

- **Held while alive:** repeater 55%, flechette 12%, rocket 12%, saber 11%,
  bryar 8%. With the runner within 1,500 u of the stand the mix is about the
  same: repeater 52%.
- **Fires at the runner:**
  - repeater alt 26, range p25 / median / p75 151 / 211 / 304 u;
  - flechette alt 9, median 121 u;
  - rocket 8, median 142 u;
  - bryar 4.

  Hit-counter rises: repeater 23, rocket 6, flechette 4. These ranges are
  shorter than the pugs' (repeater alt 280, rocket 292) because the runner
  comes straight through the defender.
- **Trip mines:** 6 laid, all on the stand at 100-409 u from it; 2 killed.
- **Pickups:** repeater 5, flechette 5, rocket launcher 2 with rocket ammo 2,
  trip mines 2, small shield 3, metallic bolts 1, power cell 1.

## Force

- **Throws:** 9 pulls and 6 pushes, at 137-751 u from the runner (median
  458 u), with 44-100 Force before each.
  - Pulls came both near the stand (122-972 u from it) and in mid.
  - Pushes came in mid and near the enemy base.
- **Other powers:** Speed 8 activations, levitation 4.

## Suicides and rearming

- **When:** all 8 of the owner's deaths were `/kill`.
  - Four came within about 1.2 s of killing the runner.
  - One came right after pulling the carrier toward a pit, at 88 s; the
    carrier died 4.5 s later.
  - One came right after the capture, at 34 s.
- **Where and how fast:** the respawns were 859-1,815 u from the stand. The
  first weapon pickup came 2-5 s after the kill, and the owner was back within
  600 u of the stand 4.2-31.7 s after it (median about 7 s).
- **What it buys:** each reset gives a fresh loadout and full Force, and puts
  the owner back in the base before the runner's next life arrives.

## A public guide, for comparison

The [jkactf basic strategy](https://sites.google.com/site/jkactf/about-ctf/basic-strategy)
pages are opinionated and have no Kejim notes. Paraphrased:

- **Roles.** A 4v4 team is usually 2 offense, 1 chase D and 1 base D.
- **Base D** ([base defense](https://sites.google.com/site/jkactf/about-ctf/basic-strategy/base-defense)):
  - hurt attackers in the base;
  - after the first attacker leaves, switch to the second enemy waiting to take
    the flag;
  - protect the own carrier when it comes home;
  - lay trip mines on the own stand at the start and again after each return;
  - selfkill to drop weapons for teammates, or to detonate det packs.
- **Chase D** ([chase defense](https://sites.google.com/site/jkactf/about-ctf/basic-strategy/chase-defense)):
  - grab a heavy weapon (rocket, repeater or Golan) that hurts through the
    saber;
  - wait at the outer edge of the base and hit attackers before they reach the
    stand;
  - respawn on purpose to arrive with more Force than the attacker;
  - pull sparingly, because Absorb feeds the target, and best when the target
    is over a gap;
  - return the flag before the carrier reaches its own base.

The owner's play matches the guide on four points: mines on the stand, heavy
splash weapons, intercepting at the base edge and beyond, and self-kill resets.
The runner has no Absorb, so pushes and pulls against it cost nothing.

## What our defenders lack

Against [base and chase D](2026-09-29-base-chase-defenders.md) and the
[fixture notes](../../../tools/fixtures/movement/defender/README.md):

1. **Rearming.** The loadout is granted once, at the release
   (`GuardSetup`). A respawned defender has only the spawn loadout and picks
   nothing up. The owner rearmed after every death.
2. **Traps.** Our defenders lay no trip mines; the owner laid six on the stand.
3. **Where to fight.** Our base D holds posts near the stand and fights the
   carrier. The owner killed 11 of the 14 runners before their pickup, and met
   them forward.
4. **Self-kill resets.** These are a human tactic and cheap for a bot: respawn
   at base when far away or out of ammo and the flag is home.
5. **Throws at the runner mid-hop over a gap.** Six of the owner's fourteen
   kills were pushes or pulls on a runner in the air over the chasm. Our
   defender throws from its stand post, where the runner can steer to a
   ledge.

The runner has its own weaknesses:
- It routes as if no defender or trip mine were there.
- It fell into the same pit after the same push twice.

Both belong to the runner's route choice ([failure attribution](2026-09-28-failure-attribution.md)).

## Limits

- **Sample:** one player, one session, 3.6 minutes, 1v1. The owner knows the
  runner's routes, and the runner has no teammate or escort.
- **Attribution:** kill causes come from time windows (fire or hit 4 s, push
  or pull 8 s), not from commands. The game log has 1 s resolution; the demo
  and the play log give the milliseconds.
- **Positions:** the runner's position between its 250 ms samples is the
  nearest sample unless the owner could see it.
- **Throw targets:** push and pull directions are not resolved. A throw counts
  toward a death only by timing.
