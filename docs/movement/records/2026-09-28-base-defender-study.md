# Base defender demo study, 2026-09-28 (draft)

Offline study of local pug demos recorded by human base players, to set the
parameters of a scripted base defender bot. Reads demos already on disk;
never connects to a server. Vocabulary, corpus and observability rules are
the position pilot's
([2026-09-27-position-pilot.md](2026-09-27-position-pilot.md)).

## The owner's defender

It positions around its own flag, shoots the attacking enemy, chases the
thief if its flag is taken, and pulls the carrier if it is getting too far
away, ideally shooting and then pulling.

## Expectations before measuring (Claude, 2026-09-28)

Written before the report was run, to be checked, not defended.

1. **Posts.** With its own flag home, base stands within 600 u of its own
   stand most of the time (median 300-500 u), above or level with it more
   often than below, and on Kejim uses two or three posts per side that
   overlook the stand's entrances. When an enemy comes within 1,500 u of the
   stand, base moves toward it (its median distance to the enemy falls
   faster than the enemy's own approach explains) rather than holding.
2. **Shooting.** Base holds a splash gun (rocket, repeater, flechette alt)
   most of the time an enemy is near, not the saber; most fires are alt
   fires of splash weapons; engagement range is 300-900 u. Rocket and
   repeater alt dominate kills. Inferred hit rate per fire (a pain on the
   aimed enemy) is under 40% for every weapon. Most fires come while the
   enemy approaches, before the steal.
3. **Chase.** Base follows the thief for a few seconds but stops within
   about 1,500 u of its own stand (it leaves the long chase to the chase);
   distance to the thief grows after about 3 s.
4. **Pulls.** Most base pulls target the enemy nearest the view ray within
   1,024 u; pulls of the carrier happen at 500-900 u with the carrier
   receding (positive range rate). The shoot-then-pull combo happens in
   under a third of carrier pulls with a gap of 200-800 ms; outcomes (pit,
   kill, drop) are more frequent after the combo than after a pull alone.
5. **Other.** Base's Speed is used mostly when its own flag is taken, to
   reach the thief.

## Tool and corpus

Tool `tools/base_defender_study.py` (`select`, `extract`, `report`;
`scan2v2`, `select2v2`, `extract2v2`, `report2v2 [--uncapped]`). Outputs
`.codex-research/base-defender-study-20260928/` (`report.txt`,
`report2v2.txt`, `report2v2-all.txt`, `fires.csv`, `pulls.csv`,
`posts.csv` and their `2v2_` twins, `scan2v2.csv`).

- **4v4 base POVs:** every `Bas` demo in the index on Kejim and Dantooine
  v3, at most 3 per recorder, one per match: Kejim 22 demos (17 recorders,
  443 live minutes), Dantooine v3 18 (14 recorders, 361 minutes). No parse
  errors.
- **Extract v4** adds to the pilot's timeline: the POV's fire events
  (playerState `EV_FIRE_WEAPON`/`EV_ALT_FIRE`, weapon, alt flag, view
  angles), the POV's hit counter (`PERS_HITS`, which `G_Damage` raises by one
  at each damaging hit on an enemy; `PERS_ATTACKEE_ARMOR` carries that
  victim's health and armor), view angles and weaponstate at every sample,
  others' current weapon, view angles at push/pull spends, and a sample at
  every snapshot (~33 ms) while an enemy is within 1,500 u of the POV.
  Playerstate events lost to the two-slot ring: 140 (Kejim) and 193
  (Dantooine) of all event types against 8,198 and 5,802 fires. Missiles
  were not extracted: the hit counter attributes hits better.

**Validation.** Pulls counted from Force drops with the pull hand extend
match the sheet's `Pull`: Kejim 788/789 (per-demo |diff| median 0, max 5),
Dantooine v3 453/452 (max 1); pushes 168/157 and 212/211. The hit counter
per fire (44% and 52%) is not the sheet's `Acc` (mean 54 and 64; the
base_enhanced definition was not checked), so hit rates below are this
study's, not the sheet's.

**Observed vs inferred.** Observed: positions, view angles, fire events,
hit-counter increments, pain and obituary events, flag status, the POV's
Force. Inferred: which enemy a fire or pull was aimed at (the enemy nearest
the view ray: within 25 degrees for fires; for pulls, the own-flag carrier
if it is in the pull's reach, else the enemy nearest the ray, as
`ForceThrow` at level 3 takes every enemy in a 1,024 u box in the front 180
degrees); line of sight (brush columns, Kejim and Dantooine only); a hit
(a pain event on the aimed enemy within flight time + 300 ms); outcomes by
3 s windows.

## Findings, 4v4 base POVs

### Q1. Position while the own flag is home

The own flag is home only 23% (Kejim) and 29% (Dantooine) of the base's
alive time in these pugs; taken 63-69%. With it home (91 and 92 minutes):

| | Kejim | Dantooine v3 |
| --- | --- | --- |
| 2D distance to own stand, p25 / med / p75 | 365 / 797 / 2,112 | 397 / 885 / 1,556 |
| within 512 / 1,000 / 1,500 u | 35 / 58 / 65% | 31 / 57 / 74% |
| height vs stand: above +64 / level / below -64 | 2 / 49 / 49% | 10 / 64 / 27% |
| bearing: toward the enemy stand (±45°) / sides / behind | 65 / 26 / 6% | 58 / 25 / 12% |

**Kejim posts** (weighted k-means, k=5, samples within 1,500 u of the own
stand; share of all own-flag-home alive time; stands red (152,-1160,1924),
blue (2464,5312,1924)). The map is point-symmetric about (1308,2076) and
the two sides' posts agree to about 100 u under that rotation.

| Red post x y z | share | to stand | Blue post x y z | share | to stand |
| --- | --- | --- | --- | --- | --- |
| 195 -952 1926 | 24% | 213 | 2429 4988 1922 | 21% | 326 |
| 634 -1235 1810 | 16% | 488 | 2090 5402 1850 | 16% | 385 |
| 274 -181 1805 | 12% | 987 | 2289 4314 1770 | 12% | 1,013 |
| -256 -1343 1861 | 10% | 447 | 2801 5476 1879 | 10% | 375 |
| -551 -741 1575 | 4% | 818 | 3162 4892 1595 | 3% | 815 |

About 35-40% of home time is farther than 1,500 u from the stand (mid).

**Enemy near the own stand.** The base's median distance from its stand
falls as the nearest enemy closes (Kejim: none within 1,500 u 1,692;
enemy 1,000-1,500 u 874; enemy under 1,000 u 609; Dantooine 1,330 / 944 /
573). Its own motion over the next 500 ms is toward the nearest enemy
(>100 u/s) 42% of the time with no enemy near and 51% with one under
1,000 u (Dantooine 52% to 55%); closing on the stand 42% to 48% (44% to
53%). So base is mostly at home when attackers are, and drifts toward the
enemy rather than holding still; the shift in motion is small.

### Q2. Shooting

Weapon held while an enemy is within 1,500 u of the own stand (Kejim 278
minutes, Dantooine 201): Kejim bryar 38%, rocket 16%, repeater 16%,
flechette 15%, disruptor 6%, saber 5%; Dantooine bryar 37%, repeater 22%,
rocket 11%, saber 7%, thermal 6%. Fires then: 22.7 and 21.5 a minute.

| Weapon (Kejim) | fires/min near | aimed range p25 / med / p75 | pain on aimed enemy | hit counter per fire |
| --- | --- | --- | --- | --- |
| repeater alt | 5.9 | 174 / 280 / 478 | 51% | 0.72 |
| bryar alt (charged) | 4.7 | 193 / 390 / 1,184 | 17% | 0.15 |
| rocket | 3.5 | 176 / 292 / 503 | 47% | 0.66 |
| flechette alt | 3.2 | 155 / 265 / 486 | 43% | 0.41 |
| flechette primary | 0.8 | 291 / 423 / 661 | 21% | 0.45 |
| disruptor alt (scoped) | 0.7 | 980 / 1,593 / 2,348 | 30% | 0.55 |

Dantooine is the same shape (repeater alt 8.0 a minute, bryar alt 4.2,
rocket 2.7, thermal alt 1.1; repeater alt range median 265, pain 53%;
rocket 269, 50%; thermal alt 298, 68%). A splash hit can count on several
players, so hit counter per fire is not a probability.

- **Kills** by the base POV (obituaries): Kejim 793 (pit 242, rocket
  splash 113, repeater alt splash 89, flechette alt splash 83, bryar alt
  66, sniper 56); 245 were own-flag carriers, 431 within 1,500 u of the own
  stand. Dantooine 648 (pit 206, repeater alt splash 93, rocket splash 76).
- **Before or after the steal.** Of 994 (Kejim) steals with the thief
  visible, the base fired at the eventual thief in the 10 s before in 46%
  and in the 10 s after in 53% (Dantooine 46% and 54% of 848). Most of the
  base's aimed fires come while its own flag is out (Kejim 4,079 against
  1,233 home), but only 36% of those are at the carrier: in 4v4 it fights
  the escort too.

### Q3. After the own flag is taken

Thefts with the thief visible within 1 s and the POV alive at the steal
(Kejim 903, Dantooine 768). Base does chase: it reaches the thief (within
400 u) in 75% and 78%, and its farthest point from the own stand in the
first 20 s has median 2,790 u on Kejim (p25 1,014, p75 3,808; beyond 1,500
u 69%, beyond 3,000 u 44%) and 2,110 u on Dantooine. It loses ground after
about 4 s: POV-thief median 839 (0 s), 714 (1 s), 811 (4 s), 1,109 (6 s),
1,535 (8 s), 1,885 (10 s) on Kejim. The chase POVs of the pilot (Kejim 372
thefts) instead close to about 500-630 u from 4 s to 8 s and follow to a
median 3,376 u. Median turn-back time (the base's farthest point, when the
theft lasted 2 s longer) 2.75 s on Kejim, 5 s on Dantooine.

### Q4. Pulls and pushes

Almost every pull has an enemy in reach (98%), usually one (median 1), a
median 14 degrees off the view ray; line of sight plausible in 95-96%.
Pulls happen mostly while the own flag is taken (Kejim 609 of 788) and
away from the stand (POV median 1,504 u from it).

| Pull target (Kejim; Dantooine) | n | distance p25 / med / p75 / p90 | range rate med; receding > 50 u/s | fired at it within 1.5 s before |
| --- | --- | --- | --- | --- |
| own-flag carrier | 357; 210 | 355 / 502 / 685 / 872; 356 / 492 / 669 / 864 | 0 u/s, 44%; 31 u/s, 48% | 20%; 20% |
| other enemy | 416; 236 | 310 / 497 / 674 / 855; 236 / 385 / 588 / 751 | -177, 29%; -153, 28% | 16%; 11% |

- **Carrier pulls are not triggered by a widening gap.** The carrier is
  moving away from the own stand (median +479 u/s) but the base keeps pace:
  the range to the base is as often closing as opening in the 500 ms before
  the pull. Non-carrier pulls are mostly on an approaching enemy.
- **Shoot then pull.** 20% of carrier pulls follow an aimed fire at the
  carrier within 1.5 s (Kejim weapons flechette alt 20, bryar alt 18,
  repeater alt 12, rocket 9). The gap is never under about 800 ms (p10 825,
  median 1,023, p75 1,188 on Kejim; median 990 on Dantooine) because
  `ForceThrow` refuses while `weaponTime > 0` (w_force.c): the pull waits
  out the weapon's refire time (800 ms for bryar, repeater and flechette
  alt, 900 for rocket). Humans pull about 100-250 ms after it ends.
- **Outcome within 3 s**, carrier targets on Kejim: combo 72 - nothing 69%,
  pitted 21%, killed by the POV 4%; pull alone 285 - nothing 66%, pitted
  21%, killed 7%. Dantooine: combo 41 nothing 63%, alone 169 nothing 62%.
  **The combo shows no advantage** over a pull alone in 4v4.
- **Pits.** 68% (Kejim) and 54% (Dantooine) of pulled carriers were within
  128 u of a pit column; those were pitted within 3 s 22% and 28% against
  19% and 15% for the rest. 500 ms after the pull the carrier is nearer a
  pit in only 33-34%: the direction of the pull relative to the pit is not
  resolved by this measure.
- Pushes are rarer (168 and 212), mostly at approaching non-carriers
  (median range rate -521 u/s), and put targets in pits more often (20-31%
  of pushes alone, 41-51% when the target is within 128 u of a pit).
- Force before a pull: median 44 (Kejim), 42 (Dantooine); p10 25.

### Q5. Speed

Base Speed 4.9 (Kejim) and 5.1 (Dantooine) activations a minute. 70% and
64% come while the own flag is taken, which is its share of time (69% and
63%): no concentration on the steal. At activation the base is on its
stand 42%, in its base 26-31%, mid 23-29%.

## Findings, 2v2 defending moments

**Corpus.** A partial parse (first four minutes, team sizes every 5 s) of
the 3,323 unlabelled or unknown-map index demos: 76 with 2 against 2 in
60%+ of samples on a map with a fixture flag stand (Kejim 46, Nelvaan 18,
ctf4 7, Talay 3, Dantooine v3 1, Kharzet v3 1). One recorder made 29 of the
Kejim ones. Capped at 3 per recorder name per map: Kejim 18 demos (12
names, 308 live minutes), other maps pooled 22 (341 minutes). One match per
recorder could not be enforced (no match ids). `--uncapped` (all 76) gives
the same picture (`report2v2-all.txt`).

**Who defends (inferred).** The POV is its team's defender at a moment
when, with its own flag home, it is alive and nearer its own stand (2D) than
its teammate (a dead teammate counts as farther, an unseen one excludes the
moment); with the own flag out, the same test at the steal. The POV
defends in 46% of its live alive time on Kejim (130 minutes), 28% on the
other maps (88). Only the POV's own fires and pulls are observable, so
everything below is the POV in those moments.

- **Q1.** Kejim, own flag home (65 minutes): distance to stand median
  1,388 (p25 631); 42% within 1,000 u; with an enemy under 1,000 u, median
  501. Posts: red (142,-1056,1934) 18% of home time, (194,-143,1797) 16%,
  (631,-1335,1841) 6%; blue (2400,5217,1926) 19%, (2353,4323,1794) 15%,
  (1781,5158,1664) 8%. The defender sits on the stand or on a forward post
  about 1,000 u toward mid, and is out in mid about half the time.
- **Q2.** Kejim with an enemy within 1,500 u of the own stand (45
  minutes): held rocket 24%, repeater 23%, bryar 21%, saber 16%; 25 fires a
  minute (repeater alt 9.5, rocket 7.1, flechette alt 2.5, bryar alt 2.2).
  Ranges and pain rates as in 4v4 (repeater alt median 302, 47%; rocket 308,
  49%). After a steal 85-91% of its aimed fires are at the carrier. Fires at
  the thief: before the steal in 56% (other maps 45%), after in 70% (81%).
  Kills 220 on Kejim (pit 72, rocket splash 40, repeater alt splash 27); 109
  were carriers.
- **Q3.** The 2v2 defender is also the chase: Kejim 205 thefts, reaches the
  thief in 86%, stays a median 430-770 u from it through 10 s, and follows
  it a median 3,592 u from the own stand (beyond 3,000 u 57%). Other maps:
  313-747 u, median 2,249 u.
- **Q4.** Kejim 281 pulls (156 on the carrier). Carrier pulls: distance
  p25 337, median 513, p75 668, p90 864; range rate median +19 u/s,
  receding 46%; the carrier moving away from the stand median +408 u/s.
  Shoot then pull in 26% (gap median 1,089 ms, p10 858; repeater alt,
  rocket, bryar alt). Outcome within 3 s: combo (41) nothing 63%, killed by
  the POV 22%, pitted 12%; alone (115) nothing 71%, killed 9%, pitted 17%.
  Uncapped (103 against 315): killed by the POV 17% against 9%, nothing 65%
  against 68%. Other maps: 93 carrier pulls, median 455 u, receding 52%,
  combo 26%, outcomes alike.
- **Q5.** Speed 3.5 a minute on Kejim while defending, 41% of it with the
  own flag home and no enemy near.

## Expectations checked

1. **Mostly wrong.** Base is not within 600 u most of the time: median
   800-900 u, 31-35% within 512 u; it is level with or below the stand, not
   above (Kejim 49% below). It does use a few posts (five clusters cover
   60-65% of home time on Kejim). With an enemy near it is nearer the stand
   and moves toward the enemy somewhat more (42% to 51%); not a hold.
2. **Partly.** The pistol is held most (38%) but most fires are splash
   alt modes (repeater alt, rocket, flechette alt); the saber is rare (5-7%
   in 4v4, 16% in 2v2 Kejim). Engagement is closer than expected (median
   265-390 u). Pits kill more than any gun; rocket and repeater alt lead
   the guns. The hit rate is over 40% for repeater alt, rocket, flechette
   alt and thermal. Fires at the thief are as common after the steal as
   before (46% against 53% of steals).
3. **Wrong.** Base follows past 1,500 u in 61-69% of thefts (median
   farthest point 2,110-2,790 u), but falls behind the thief after about
   4 s, while the chase stays on it. In 2v2 the defender chases as far as a
   chase.
4. **Partly.** Carrier pulls at a median 500 u (p25 355, p90 870), not on a
   receding carrier more than an approaching one; the combo is 20-26% (under
   a third), but the gap is 800-1,400 ms, not 200-800 (the refire rule);
   the combo does not improve outcomes in 4v4 and adds kills by the POV in
   2v2 Kejim.
5. **Wrong.** Speed use follows time share: 64-70% while the own flag is
   taken, matching the 63-69% of time.

## Limits

- Targets, hits and outcomes are inferred from geometry and time windows;
  a splash fire aimed at the feet may be assigned to no one (31-36% of
  fires have no enemy within 25 degrees). Mines and det packs are placed,
  not aimed.
- Pit direction of a pull (toward or away from the edge) is not measured;
  only pit distance before and 500 ms after.
- Dantooine v3 has culled enemy visibility (pilot: 65% of enemy-time);
  Kejim 99%. LOS and pit columns ignore Dantooine's patch floors.
- 2v2 roles are behavioural; 2v2 demos are dominated by a few recorders
  and are not pugs with sheets, so no stat-sheet validation there. The
  other-maps pool mixes five maps.
- The sheet's `Acc` definition was not checked against the hit counter.
