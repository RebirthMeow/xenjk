# Demo position pilot (roadmap step 2), 2026-09-27

Offline study of local pug demos: can the snapshots answer what the CTF brain
needs to know about the four positions? Reads demos already on disk; never
connects to a server.

## Vocabulary (owner, 2026-09-27)

Given before any analysis, as definitions, not findings.

- **Teams of four:** two offense, one base, one chase. Base and chase are
  the defenders.
- **Chase:** returns the flag when it is stolen.
- **Base:** keeps the base clear of enemies and supports the allied flag
  carrier (FC) when it comes home. It balances keeping its own base clear
  against supporting in mid and the enemy base, and, when supporting,
  between supporting the chase and the offense.
- **Offense:** caps: steals the enemy flag, brings it home and caps. Of the
  two, the one without the flag sets up to grab the enemy flag quickly if it
  is returned, and helps the chase return their own flag.
- **Minute-to-minute context** is in team overlay information, chat binds
  and the like. Behaviour depends on state: a team whose FC is low (judged
  from the average damage per hit) plays differently from one whose FC has
  full health and Force; the "hustle" against an escaping enemy differs
  between one in your base and one crossing mid.

The owner holds further ideas back until the initial findings, so they do
not steer them.

## Expectations before looking (Claude, 2026-09-27)

Written before any per-position analysis, to be checked, not defended.

1. Base spends most of its time in its own base and flag stand (the stat
   sheet's `Fs`+`Bas` over 60%); chase less so, offense least.
2. After a steal the chase leaves its current zone toward the carrier within
   about 2 s, faster when the steal is seen in its own snapshots than when
   known only from the flag status.
3. While the team's FC is running, the non-carrying offense stays nearer the
   enemy base than the FC does (set up for the regrab), unless its own flag
   is also taken.
4. Speed is used mostly by offense on the way in and by chase after a steal;
   absorb and protect mostly by the FC under fire; team heal and energize by
   base or chase near a low FC.
5. A low FC (by team overlay) draws the base out of its base more often than
   a full-health FC does.
6. Team chat binds carry health, armor and Force for part of the players
   only; team overlay covers every teammate.

## What the demos carry (first probe, same day)

- **`tinfo` (team overlay)** about every eighth snapshot: per teammate the
  client, location index, health, armor, weapon and powerups. Location
  indexes name item-derived places (`Red Flagstand`, `Mid`, `Blue Rockets`,
  `Pit`; Dantooine v3 configstrings 1227-1254).
- **Team chat binds** with numbers (a Kejim demo: a red, green and blue
  triple that reads like health, armor and Force).
- **Enemy visibility is map-dependent.** A Kejim demo has all seven other
  players in 38,848 of 38,894 snapshots; a Dantooine v3 demo has from 0 to 7,
  so there an enemy exists only while the POV's snapshot carries it.
- Mod commands `scores`, `ircg`, `kg2`, `nfr`, `rcg`, `kls`, still to decode.
- The stat sheet already has a zone vocabulary: per-player time percentages
  `Fs Bas Mid EBa EFs` (own stand, own base, mid, enemy base, enemy stand).
  Any zone classifier the pilot builds is checked against these.

## Corpus and selection

Labelled local demos (`.codex-research/human-rush/index.csv`): only Kejim
(Off 34, Bas 24, Cha 295) and Dantooine v3 (Off 45, Bas 28, Cha 247) have
about 20 per position. The pool is recorder-heavy: on those two maps, chase
542 demos from 120 players, but one recorder alone 110; base 52 from 26; offense 79
from 36. The pilot caps demos per recorder so a few players do not make up a
position.

## Initial findings (same day)

Tool `tools/position_pilot.py` (select, extract, report). 60 demos: 10 per
map and position, at most 3 per recorder, one per match; 1,159 live minutes
(warmup and pause countdowns excluded). Outputs
`.codex-research/position-pilot-20260927/` (`extract-v2`, `report.txt`; the
first 12 extracts in `extract` predate the instant-power record and are
kept, unused).

**Validation.** Two derived quantities reproduce the stat sheet: POV region
time by the stats mod's own rule (`GetCTFRegion`, base_enhanced
`g_stats.c`) differs from the sheet's `Fs Bas Mid EBa EFs` by a median 0.0
and p90 0.2 points per region over all 60 demos; push and pull counted from
the POV's Force drops and hand extend match the sheet's columns (Off
307/310 and 471/477, Bas 199/193 and 643/642, Cha 264/265 and 1040/1049).

**Observability (observed).**

- Team overlay (`g_teamOverlayForce`): every teammate's health, Force (in
  the armor slot) and armor (top byte of powerups), location name, weapon;
  a change every ~264 ms. A team always knows its own members' state.
- Enemies: in the POV's snapshots 99% of enemy-time on Kejim, 65% on
  Dantooine v3 (the server culls by visibility there). Their health is only
  ever sent as a pain tier (100/74/49/24, the pain-sound bands) at each
  visible hit: what a human hears, not the number.
- Team chat 3.6-3.7 lines a minute; about 25-30% carry numbers. The top
  templates are calls (`sk`, `cap`, `te!`, `get`, `get flag`, `capture`)
  and status binds (`>> sk (#/#/#)`, `~sk ::#::#::#`, `>> team energize
  (#/#/#) @ our mines`). Bind number meanings not yet decoded.
- Instant Force powers (push, pull, heal, team heal/energize) leave no
  active bit; spends are seen in the POV's own Force only. Labelling the
  non-push/pull spends by the selected power is unreliable: base shows 4.6
  "speed" spends a minute with no active bit, most likely team energize by
  bind (the `te!` calls, the sheet's `TE` column). Open.

**Region by position (derived, POV only, alive; the by-flag-state figures
below exclude time carrying).**

| Position | Fs | Bas | Mid | EBa | EFs |
| --- | --- | --- | --- | --- | --- |
| Off | 18% | 7% | 32% | 10% | 32% |
| Bas | 51% | 17% | 27% | 2% | 3% |
| Cha | 28% | 14% | 36% | 7% | 15% |

By flag state: chase sits on its stand while its flag is home (58% `Fs`
with both flags home) and leaves it when the flag goes (26% `Fs`, 43% `Mid`
with only its own flag taken; 22% `EFs` when both are taken, following the
thief home). Base stays: its `Fs` share falls from 64% to 40% and `Mid`
rises to 37% when its own flag is taken, but it rarely crosses (`EBa`+`EFs`
at most 6%). The non-carrying offense is at the enemy stand 45% of the time
when both flags are out, 29% when only its team's FC is running (42% `Mid`
then).

**Force by position (observed activations a minute).** Offense: Speed 4.1,
absorb 2.7, levitation 2.6, protect 0.9; while carrying mostly levitation,
absorb and protect. Chase: Speed 4.4, absorb 2.7, pull 2.7 (spends). Base:
Speed 5.4, and the only position with dark powers (drain, rage, grip,
lightning, 0.05-0.16 a minute), almost no absorb.

**Expectations checked.**

1. Base mostly home: holds (68% `Fs`+`Bas`); chase 42%, offense 25%.
2. Chase leaves within 2 s after a steal: **inconclusive**. The test
   (closes 150 units on its own stand within 500 ms) fires as often at
   random own-flag-home times as after a steal (chase away from home 90%
   baseline, 85% after), so it measures nothing; the response needs a
   target-relative measure (toward the thief or the enemy FC).
3. Non-carrying offense nearer the enemy base while its FC runs: partly;
   it depends on its own flag (45% at the enemy stand when both flags are
   out, 29% otherwise).
4. Speed mostly offense and chase: **no**; all three use it 4-5 a minute,
   base the most. Absorb and protect by the FC: holds.
5. A low FC draws the base out: **not seen**. Base region with an ally
   carrying: `Fs` 47% (FC under 50), 54% (50-99), 47% (100+). Region share
   may be the wrong measure of support.
6. Overlay covers every teammate: holds; binds cover part of the calls.

## Owner notes and pits (same day)

Owner, after the initial findings: base's team energize is how an FC at home
affords protect, Speed and push to survive; a chase reaction is about being
where the enemy is, intercepting its course, and having a viable weapon
(sabers block non-splash); pits matter a great deal: push and pull put
players into pits, and even with absorb a push or pull freezes the target,
so an offense's route depends on how exposed it is to an enemy's Force by
distance and on how much Force it thinks the enemy has left. The stat
sheets are a broad community view, not comprehensive.

**Sheet columns corrected** (base_enhanced `g_stats.c` column help): `Pit`
is pit kills, `Dth` is **pit deaths** (not all deaths), `SK` is selfkills
including falling into a pit alone (about 400 a match: the obituary stream
is mostly code 39). A pit kill is a death by crush, falling,
`trigger_hurt`, unknown or suicide credited to an enemy (`g_combat.c`):
suicide counts because the game credits the last enemy who moved you.

**Pits (observed obituaries; the POV's pit kills equal the sheet's `Pit`,
668 of 668).**

- Pits are 37-38% of enemy-caused deaths on both maps (Kejim 2,231 of
  6,000; Dantooine v3 2,231 of 5,882; the sheets independently give 2,219
  and 2,220), and 28% of flag carriers' enemy-caused deaths.
- Victims by position: offense 2,289, chase 1,077, base 946. Pit kills by
  position: offense 1,965, chase 1,431, base 954.
- 66% of the POV's pit kills (441 of 668) follow its own push or pull
  within 3 s; the rest are other routes (knockback, grip, a later
  selfkill).
- 20% of visible pit victims (776 of 3,809) had absorb active at death:
  absorb does not make a player safe at a pit edge.

## Self-kills and team energize: hypotheses before measuring (same day)

**Owner.** Weapons can be picked up once; after that only their ammo adds to
what a player holds, so a chase often selfkills to respawn and grab fresh
guns. An offense may selfkill for a better spawn, or while carrying to end
a run that is going badly and let the other offense start theirs. A base
may selfkill to regain Force or health, or to get from mid back to base.
Team energize from the base is how an FC at home affords protect, Speed
and push to survive.

**Claude, before measuring.**

a. Most deliberate selfkills (code 39, not credited to an enemy) come with
   a depleted resource: Force under 25 or health under 50.
b. A chase's selfkill is followed by at least two new weapons within 15 s
   of the respawn more often than any other position's.
c. Selfkills while carrying are rare (under 2% of selfkills) and are
   followed within 20 s by the team's other offense taking the flag.
d. A base's selfkills happen mostly outside its own base (`Mid` or beyond).
e. Team energize reaches an FC mostly at or near its own base, and the FC
   spends the gained Force within 5 s, mostly on protect, absorb or Speed.

## Self-kills and team energize: findings (same day)

Tool `tools/position_resources.py` over `extract-v3` (the POV's armor,
weapons, ammo and spawn count added), output
`.codex-research/position-pilot-20260927/resources.txt`. A selfkill here
is `/kill` (code 39) not credited to an enemy; a pit kill is excluded.
Baseline: the same position's state at every 20th live sample.

**Selfkills are routine resource management.** The recording players
selfkill 1.95 (offense), 2.60 (chase) and 4.16 (base) times a live minute,
a median 9-13 s into a life, and are back in 1,155 ms.

| POV | Force < 25 at SK (baseline) | Health < 50 | Armor 0 | Either Force or health low |
| --- | --- | --- | --- | --- |
| Off | 43% (27%) | 18% (12%) | 43% (37%) | 48% (33%) |
| Bas | 60% (24%) | 9% (5%) | 34% (23%) | 63% (27%) |
| Cha | 52% (26%) | 18% (8%) | 49% (29%) | 57% (30%) |

Teammates' selfkills by overlay agree (Force under 25: base 51% against
21%, chase 50% against 26%, offense 38% against 26%). Force is the
resource most often spent out before a selfkill; health much less.

- **Base** (hypothesis d, wrong): selfkills where it usually is, 47% on
  its stand, 30% in mid (baseline 51% and 27%). Getting home from mid is
  not over-represented; refilling Force is (60% against 24%).
- **Chase** (hypothesis b, partly): selfkills with more guns than usual
  (3+ guns 37% against 30%) and low ammo (51% against 43%); after the
  respawn it picks up a gun within 15 s 82% of the time and two or more
  35%, the most of any position (offense 72% and 28%, base 63% and 17%).
- **Offense**: selfkills on its own flag stand 40% of the time against
  19% baseline: after a capture or a return, a selfkill buys a respawn
  (41% of offense respawns are in mid) instead of a walk back out.
- **Carriers** (hypothesis c, rate wrong, handoff right): 152 of 3,362
  POV selfkills (5%) are while carrying, 114 by offense. The carrier has
  a median 11 Force and 63 health, deep in the enemy half (median 5,484
  units from its own stand), usually with one or two enemies within 800
  units. A teammate holds the flag within 20 s 99% of the time, within
  1 s 47%, next carrier the other offense in 130 of 152: a handoff to a
  fresh teammate standing by, as the owner described.

**Team energize (hypothesis e, holds).** 10,493 received energizes (an
overlay Force rise of 15+ between two messages, no respawn), 9.05 a
team-minute, median 33 Force. Of those with a visible giver, base gives
5,217, offense 595, chase 261 (941 had several candidates; 3,383 had no
visible fall and stay unattributed). Receivers: offense 6,683, chase
3,251, base 250. Half the receivers visible at the time are carrying
(4,574 of 9,369); the carrier is on its own stand or in its base in 51%
of those, mid 29%, the enemy half 20%. 55% of receivers spend 20+ Force
within 5 s; the activations seen in that window are Speed 3,160,
levitation 2,482, absorb 1,684, protect 1,430.

Limits: guns are counted, not weapon values; an overlay rise can also be a
Force boon or drain; the giver attribution misses a third.

## Open items: hypotheses before measuring (same day)

Owner direction: chase reaction is being where the enemy is, intercepting
its course, and a viable weapon (sabers block non-splash); pits shape
offense routes by exposure to enemy Force at distance and by how much
Force the offense thinks the enemy has left.

**Claude, before measuring.**

f. Status binds with three numbers carry the sender's health, armor and
   Force from the overlay, in that order.
g. A bare `sk` call announces the sender's own selfkill within 5 s in most
   cases; a `te!` call is followed by an energize reaching the sender
   within 5 s in more than half.
h. Most kills of flag carriers are by splash weapons or pits, not saber.
i. A chase that returns the flag first reaches the thief (within 400
   units) from ahead of it more often than one that does not.
j. A carrier stays farther from pit volumes while a visible enemy is
   within 1,024 units than while none is, and a player near a pit with an
   enemy near is pitted far more often than one with either alone.

## Open items: findings (same day)

Tool `tools/position_open.py` (`binds`, `chase`, `pits`), outputs
`.codex-research/position-pilot-20260927/open-*.txt`.

**Binds (f holds).** Every common status bind carries the sender's overlay
health, armor and Force in that order (first number = health in 93-100%;
the rest match armor and Force, "both" where the two are equal). Energize
binds add the sender's location name (`>> team energize (h/a/f) @ our
mines`). Calls, each against the same test 30-35 s later as a control:

| Call | n | Sender | What follows within 5 s (control) |
| --- | --- | --- | --- |
| `te` | 267 | carrying 92% | sender energized 61% (24%) |
| `help` | 50 | carrying 98% | sender energized 52% (26%) |
| `sk` | 857 | carrying 1% | another teammate selfkills 75% (50%); sender 3% (11%) |
| `cap` | 426 | carrying 4% | sender selfkills 55% (23%) |
| `capture` | 314 | carrying 2% | sender selfkills 49% (14%) |
| `get` | 1,019 | carrying 49% | own flag taken 93% at the call; nothing distinct follows |

So `te` and `help` are the carrier's requests and are answered; `sk` asks
a teammate to selfkill (g wrong: it is not an announcement); `cap` and
`capture` are said by a non-carrier just before its own selfkill while
both flags are out. What `cap` means is the owner's to say.

**Carrier kills (h holds).** 3,336 kills of a visible carrier by an enemy:
splash or explosive 50%, pit 28%, direct gun hits 20%, saber 2%. Chase
makes 1,953 of them (pit 579, rocket splash 371, repeater alt splash
229, flechette alt splash 211).

**Thefts and interception (i: leaning, not shown).** 880 thefts seen by a
chase POV, 999 by a base POV. Only 5-8% end in a capture; most end with
the flag dropped (the carrier killed) or returned. The chase reaches the
thief (within 400 u) in 85%, a median 2.3 s after the steal, mostly at its
own stand or base (563 of 744), where ahead or behind means little. Away
from home the chase met the thief from ahead in 113 thefts and from
behind in 68; captures followed in 12% and 18%. The direction is the
owner's, the counts too small to call it. The chase held a gun at 711 of
744 contacts.

**Pit exposure (j: second half holds, first half does not).** Pit geometry
from the BSP: each map's pit is one `trigger_hurt` slab under the map
(Kejim top z 704, Dantooine v3 -128); a 32-unit column is over the pit at
a player's height when no walkable brush top lies between the slab and the
feet and no brush fills that height. Validation: 88% of falling or pitted
victims were within 64 u of such a column 400 ms before death, 99% within
192 u. Dantooine v3's 781 curved patch floors are not brushes, so its pit
columns are overstated there; Kejim has none. Pits are close almost
everywhere an offense goes (median 120-146 u, 98-99% of samples within
384 u), so distance alone separates little.

- Carriers do not keep farther from pits with an enemy within 1,024 u
  (median 141 u) than without (138 u).
- Being pitted within 3 s: pit under 128 u and enemy near 5.65%; pit near
  alone 1.85%; enemy near alone (pit 128-384 u) 1.50%; neither 0.62%. The
  two exposures multiply.
- Not yet measured: the enemy's remaining Force. It is never sent; it can
  only be estimated from the enemy's visible activations since its spawn.

## Pilot status (same day)

Answered from the demos: team state (overlay), enemy state as seen or
heard (position, flag, active powers, pain tier), region and flag-state
occupancy, Force use by position, pits and who causes them, selfkill
purposes, team energize flow, bind contents and call responses, carrier
kill weapons. Short of data: interception away from home (about 180
cases over 60 demos), rare situations by map. Not observable: enemy Force
and health numbers, anyone's intent, instant powers of players other than
the POV (push and pull only by their effect or animation, not decoded).
Waiting on the owner: the protect-change thread export, and what the `cap`
call means.

## Correction: energize and absorb gains (same day)

The energize counts above mixed two things. Single-receiver overlay Force
rises cluster at 49-51 (a teammate's Force fell with every one: team
energize, 50 Force as the community states) and at 17-18 (the receiver had
absorb on in every visible one; a teammate paid in only 22%: Force gained
by absorbing an enemy's push, pull, grip or drain). The rows with no
visible giver were mostly absorb gains. `tools/position_resources.py` now
counts a rise as energize only when a teammate's Force fell with it:

- 7,110 energizes, median 49 Force, 6.1 a team-minute; base gives 5,217 of
  those with one giver (offense 595, chase 261; 941 had several).
- Receivers: offense 4,429, chase 2,313, base 191. Half go to a carrier
  (3,161 of 6,345 visible); the carrier is in its own stand or base 65%,
  mid 26%, the enemy half 9%.
- 60% of receivers spend 20+ Force within 5 s (Speed, absorb, levitation,
  protect).
- Absorb gains, counted apart: offense 2,254, chase 938, base 59.

Community discussions and their checks against these demos:
[2026-09-27-community-discussions.md](2026-09-27-community-discussions.md).

## Owner answers (same day)

- **`cap`** means "capture the flag": a signal to one's own carrier,
  usually from the chase or the camper, that the enemy carrier is falling
  into a pit or already dead and the own flag is about to be returned. The
  caller's selfkill that often follows (55% against 23%) is its job done.
- **Absorb, not protect, at pit edges**: absorb stops pushes and pulls;
  protect is for when that is not needed, or when health is too low to
  survive otherwise even at risk. The carriers' powers by pit distance and
  health agree ([community record](2026-09-27-community-discussions.md#checked-against-the-pilot-demos)).

## The camper's trade: hypotheses before measuring (same day)

Owner: players camp differently; the sheets' damage to the enemy carrier
(`Dmg Fc`) by offense players shows who had chances and took them, and a
low value can just mean long holds (that offense was the carrier).

**Claude, before measuring.**

k. Offense players' `Dmg Fc` per minute not holding varies widely between
   players (the top quarter at least twice the bottom quarter), and a
   player's rate is stable from match to match (its match-to-match spread
   is smaller than the spread between players).
l. Within a team, the offense with less hold time has the higher `Dmg Fc`.
m. In demos, when an offense's own carrier dies in the enemy half, a
   camper already within 1,000 units of the enemy stand retakes within
   10 s more often than one farther away, and a camper farther away (with
   the chase) is more often present at the return of its own flag.

## The camper's trade: findings (same day)

Tool `tools/position_camper.py`; outputs
`.codex-research/position-pilot-20260927/camper-*.txt`.

**Stat sheets** (3,414 sheets whose first table has `Dmg` and `Hold`;
13,462 offense rows with 5+ minutes not holding). Damage dealt to the enemy
carrier per minute not holding:

- **k (mostly not).** The 49 players with 10+ such matches have median
  rates p25 18.9, median 22.8, p75 33.8, p90 45.0 a minute: the top quarter
  is 1.8 times the bottom, short of the 2 expected. A player's
  match-to-match spread (median sd 13.3) is larger than the spread between
  players (sd 11.8): styles differ, but the match decides more than the
  player does.
- **l (weakly).** In 6,728 offense pairs with different hold times, the
  one who held less dealt more carrier damage in 56%: hold time explains
  part of a low value, as the owner said, not most of it.
- The team's camper (the offense who held less), by its rate: bottom
  quarter (under 17.4 a minute) team won 46% of 1,671 decided matches,
  middle half 49%, top quarter (43.5+) 56%. An association only: a team
  that is winning also gives its camper more chances.

**Demos** (the 20 offense POVs, not carrying at the moment):

- **m, first half (weakly).** When the own carrier drops the enemy flag,
  the team retakes within 10 s in 34% with the POV within 1,000 u of the
  enemy stand (125) against 29% farther away (171).
- **m, second half (not seen).** When the own flag comes home while the
  team holds the enemy flag, the team caps within 20 s in 13% (POV near the
  enemy stand, 107) and 14% (farther, 175). Where the camper stood made no
  visible difference to this.

Reading: camping style shows in the sheets but is noisy per match, and a
first cut at the demos finds only small effects of the camper's position.
The trade the owner describes is situational; measuring it needs the
situation (both carriers' health and Force, the chase's state, distances)
rather than position alone, and more offense demos than 20.

