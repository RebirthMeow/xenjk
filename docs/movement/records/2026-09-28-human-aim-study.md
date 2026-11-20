# Human aim study, 2026-09-28 (draft)

Offline study of local pug demos: how human players aim each weapon, to set
the parameters of a humanized bot aim model. Reads demo extracts on disk;
never connects to a server. Builds on the base defender study
([2026-09-28-base-defender-study.md](2026-09-28-base-defender-study.md)),
whose v4 extract carries the POV's fire events, view angles and hit counter.

## Expectations before measuring (Claude, 2026-09-28)

Written before the report was run, to be checked, not defended.

1. **Aim point.** For fast projectiles (pistol, blaster, repeater primary,
   flechette primary) and hitscan disruptor, humans aim at the target's
   current center, not a lead point (the lead is under 3 degrees at typical
   ranges, below their error). For rocket they aim nearer the floor under
   the target than its center. For repeater alt and flechette alt they aim
   above the center (the arc). Median error 3-6 degrees at under 300 u,
   falling in degrees with range (1-3 degrees beyond 1,000 u); p90 about
   three times the median.
2. **Hit rate** per fire falls with range for every projectile weapon; for
   splash weapons it is above 50% under 300 u. Airborne targets are hit
   less often than grounded ones.
3. **Turning.** Tracking yaw speeds median 30-80 deg/s with peaks (p99)
   above 500 deg/s; the view converges within 10 degrees of a newly
   appearing enemy in 300-500 ms and within 5 degrees in 500-800 ms.
4. **Reaction.** First fire at an enemy that enters 1,500 u with line of
   sight: median 400-800 ms; after a steal seen by a defender, the first
   fire at the thief median under 1 s.
5. **Cadence.** Humans fire splash weapons near their maximum rate (median
   gap within 1.2 times the refire time) and pace the charged pistol and
   disruptor (gaps over 1.5 times the refire time).
6. **Error model.** The horizontal error is mostly a lag behind the target
   (signed error opposite to the target's angular motion), about 80-150 ms
   of tracking delay, and grows with the target's angular speed as seen
   from the shooter.

## Tool and corpus

Tool `tools/human_aim_study.py` (`extract`, `report`); outputs
`.codex-research/human-aim-study-20260928/` (`report.txt`, `fires.csv` with
one row per attributed fire, `extract-v4-pilot`). Corpus, 120 demos: the 40
4v4 base POVs and the 40 capped 2v2 demos of the base defender study, and
the position pilot's 40 offense and chase demos re-extracted to v4 here
(Kejim and Dantooine v3, plus ctf4, Nelvaan, Talay and Kharzet v3 from 2v2).
31,617 fires of the studied modes; no parse errors.

**Method.** A fire is attributed to the live enemy whose center (origin +8)
or ideal lead point is nearest the POV's view ray, within 30 degrees: 82-96%
of fires per mode (uncharged pistol 50%: mostly spam at over 1,000 u).
Enemy velocity is the finite difference of snapshot positions over about
100 ms; airborne means |vertical speed| > 100 u/s. Ideal lead: straight
intercept at the g_weapon.c projectile speed; gravity arc (800) for repeater
alt, flechette alt (mean speed 1,050) and thermal alt; the floor point is
the feet (origin -24) at the lead. A hit is a hit-counter increment
attributed to the latest fire within 3 s. View angles are the snapshot's
after the fire; the previous snapshot's angles give the same medians.

Not observed or too few: blaster primary (no fires), saber throw (4 saber
alt events), DEMP2 (2 primary). Bowcaster (114 and 104 attributed) and
blaster alt (190) are thin.

## Findings

**1. Aim point.** Humans aim at the target's current position, without
flight-time lead, for every projectile weapon, and at where the target was
50-100 ms earlier for the hitscan disruptor.

- The signed horizontal error against the target's bearing rate has slope
  -3 to +13 ms for repeater alt, rocket, charged pistol, flechette alt and
  thermal alt, where the ideal lead is 250-690 ms of flight; the POV is
  behind the target in 41-47% of moving cases (no systematic side).
- Median error to the target's past position is lowest at 0 ms for the
  projectiles and at 50-100 ms for the disruptor (4.35 at 0 ms, 3.75 at
  100 ms; scoped 1.75 to 1.33), which is behind the target in 77-80% of
  moving cases (slope -46 and -59 ms).
- Vertically: repeater alt and flechette alt are aimed 2-4 degrees above
  the center (the arc needs 13-14); rocket 1.7 degrees below (p75 7.6);
  hitscan and fast guns 1-3 degrees above.
- The lead point is no better than the center except for rocket (15.6
  against 17.9 median) and charged pistol (10.0 against 10.9); for the arc
  weapons it is much worse (thermal alt 35 against 18).

| Mode | attributed | center error med / p90 by range <300, 300-600, 600-1000, >1000 u | hit rate same bands |
| --- | --- | --- | --- |
| repeater alt | 7,953 | 16.4/28.7, 12.4/26.1, 14.0/29.4, 15.9/29.9 | 77, 55, 42, 37% |
| rocket | 5,547 | 18.8/34.1, 16.4/32.2, 16.9/35.4, 18.4/40.4 | 79, 53, 38, 37% |
| pistol charged | 4,610 | 12.0/24.8, 9.5/21.6, 9.2/24.0, 11.6/28.5 | 27, 12, 7, 3% |
| flechette alt | 3,171 | 16.1/29.4, 13.6/27.2, 11.0/25.2, 15.1/26.9 | 32, 19, 12, 16% |
| repeater | 1,300 | 11.1/24.6, 6.9/20.6, 5.4/20.7, 3.2/21.4 | 16, 4, 2, 2% |
| thermal alt | 1,041 | 19.6/35.5, 15.0/27.5, 16.9/29.3, 18.4/33.7 | 77, 54, 38, 32% |
| flechette | 737 | 8.8/21.5, 5.1/12.9, 4.7/11.9, 4.2/13.6 | 42, 34, 15, 16% |
| conc | 692 | 10.4/22.1, 7.0/15.2, 5.3/13.9, 6.0/14.8 | 77, 69, 66, 45% |
| disruptor | 651 | 7.6/18.4, 3.9/10.2, 2.4/6.0, 1.9/9.5 | 28, 27, 27, 26% |
| disruptor scoped | 662 | (18 under 300), 3.8/11.9, 2.0/4.2, 1.1/4.3 | -, 28, 42, 42% |
| pistol | 291 | mostly >1,000 u: 18.4/31.3 | 0% |

Splash-weapon errors are large in degrees and flat with range: they aim
near the target (feet, walls) rather than at it; their hit rate is carried
by splash. Hitscan and fast-gun errors fall with range as the target's
angular size does. The three corpora agree (repeater alt center median
14.1-15.6, hit 62-67%; rocket 17.2-18.3, 61-65%).

**2. Hit rate** (above). Airborne targets: slightly lower for repeater alt
(62 against 67%) and rocket (61 against 66%); no difference for charged
pistol, flechette, disruptor. Targets moving 600+ u/s: rocket 53% against
65-66%, repeater alt 57% against 65-67%.

**3. Turning.** Yaw speed with an enemy within 1,500 u: median 27 deg/s,
p90 206, p99 742; while tracking (gun held, view within 15 degrees of an
enemy) median 18, p90 139, p99 494; pitch median 6, p90 55, p99 182.
Convergence on an enemy entering 1,500 u with line of sight, when the POV
fires at it within 5 s (engaged): initial angle under 20 degrees, within
10 degrees after median 231 ms (89% within 3 s), within 5 degrees 726 ms;
20-60 degrees, 1,150 ms and 1,386 ms; over 60 degrees, 1,749 and 1,914 ms.

**4. Reaction.** 21,136 entries (enemy enters 1,500 u with line of sight
after 2 s out); the POV fires at that enemy within 5 s after 36% of them,
median 1,881 ms (p25 1,089, p75 3,036). After a steal with the thief
within 1,500 u of the POV: fires at the thief within 10 s in 72% of 2,172
thefts, median 1,254 ms (p25 450, p75 2,607); farther thieves 3,762 ms.

**5. Cadence** (same-mode gaps under 3 s; refire time from bg_weapons.c):

| Mode | refire | gap p25 / med / p75 | at max rate (<=1.15x) | paced (>1.5x) | shots per engagement med / p90 |
| --- | --- | --- | --- | --- | --- |
| repeater alt | 800 | 800 / 891 / 1,188 | 53% | 24% | 1 / 4 |
| rocket | 900 | 924 / 1,122 / 1,650 | 44% | 36% | 1 / 3 |
| pistol charged | 800 | 1,650 / 1,914 / 2,343 | 1% | 95% | 1 / 2 |
| flechette alt | 800 | 825 / 924 / 1,287 | 46% | 29% | 1 / 4 |
| repeater | 100 | 99 / 99 / 99 | 87% | 4% | 2 / 10 |
| flechette | 700 | 693 / 792 / 990 | 56% | 22% | 1 / 3 |
| disruptor | 600 | 627 / 693 / 891 | 49% | 23% | 2 / 5 |
| disruptor scoped | 1,300 | 1,551 / 1,782 / 2,079 | 21% | 31% | 1 / 2 |
| thermal alt | 400 | 1,023 / 1,320 / 1,815 | 0% | 99% | 1 / 3 |

**6. Error model.** For projectiles the horizontal error is noise centred
on the target's current position, growing with its bearing rate |omega| as
seen from the shooter: median |yaw error| repeater alt 8.7, 10.3, 12.2,
14.8 degrees for |omega| 0-30, 30-90, 90-180, 180+ deg/s; rocket 10.4 to
14.4; charged pistol 6.4 to 12.6; flechette 3.1 to 16.9. For the hitscan
disruptor it is a lag: 1.8, 4.5, 7.5, 11.9 degrees, about 50-100 ms of
tracking delay.

## Expectations checked

1. **Partly.** Center, not lead, for fast projectiles and hitscan: holds
   (hitscan with a 50-100 ms lag). Rocket slightly below center and the
   arcs slightly above: holds in direction, far short of the ideal arc.
   Errors 3-6 degrees under 300 u: wrong (7.6 disruptor to 19 rocket);
   they fall with range only for hitscan and fast guns; p90 is about twice
   the median, not three times.
2. **Mostly holds.** Hit rate falls with range for every projectile
   (repeater alt 77% to 37%), not for the disruptor (flat 26-28%); splash
   under 300 u 77-79%. Airborne targets are hit only slightly less.
3. **Partly.** Tracking yaw speed median 18 deg/s (lower than 30-80), p99
   494-742 (holds). Convergence within 10 degrees in 300-500 ms only when
   the enemy appears within 20 degrees of the view (231 ms); from 20-60
   degrees it takes 1.15 s.
4. **Wrong.** First fire after an enemy appears: median 1.9 s, not
   0.4-0.8 s; after a nearby steal 1.25 s (p25 450 ms), not under 1 s.
5. **Mostly holds.** Splash near max rate half the time (median gap 1.11x
   repeater alt, 1.25x rocket); charged pistol and scoped disruptor paced
   (95% and 31% over 1.5x; the pistol's charge takes the time).
6. **Partly.** Lag only for hitscan (50-100 ms); projectiles show neither
   lag nor lead. The error grows with angular speed: holds.

## Limits

- The view angles are the snapshot's after the fire (up to about 33-50 ms
  later); at 500 deg/s that is several degrees. The previous snapshot gives
  the same medians, so the figures are not dominated by it.
- The server's positions are not what the shooter saw: the client draws
  enemies interpolated about one snapshot plus its ping behind. The hitscan
  lag is that delay (the server's unlagged hitscan, if enabled, compensates
  it; not checked); projectiles are not compensated, so aiming at the
  current server position is itself a lead of about that delay.
- Attribution to the nearest enemy within 30 degrees biases errors low and
  drops wild shots (4-18% unattributed); splash shots aimed at walls or
  floors near a target count as large errors.
- Enemy velocity is a 100 ms finite difference; airborne by vertical speed
  only. Line of sight uses brush columns (Kejim, Dantooine only); other maps
  count entries by distance alone. Entry is not what the player saw, which
  inflates reaction and convergence times.
- The hit counter credits a hit to the latest fire within 3 s; a splash
  that damages several players counts once per fire here.
