# Defenders updated from the owner's 1v1 (2026-09-29)

Owner: "update defenders based on learnings". The learnings come from the
[human defense session](2026-09-29-human-defense-session.md), and the role
split was agreed in chat:

| Behaviour | Base D | Chase D |
| --- | --- | --- |
| Rearm after a death, self-kill reset | yes | yes |
| Trip mines on the stand | yes | no |
| Meet the runner forward, throws at the chasm | no | yes |

All four are in `bot2_defender.c`. Each is off by default and switched on by
a cvar; the bench and laps options below set them.

## Behaviours

- **Rearm** (`bot_defenderRearm`, `--defender-rearm`).
  - **Trigger:** a loadout weapon it lacks, or one with under three shots of
    ammo. A respawn keeps only the spawn loadout, because the fixture grants
    the loadout only at the release.
  - **Target:** the nearest item it can grab (`BG_CanItemBeGrabbed`) that
    gives that weapon or its ammo, within 1,500 u of its home (the stand for
    base D, the post it holds for chase D) or 700 u of where it stands.
  - **Movement:** the planner, then a straight walk over the last 72 u.
  - **Limits:** it gives up after 8 s and skips that item for 20 s. It does
    not rearm while its role is busy: base D with the runner seen within
    1,500 u of the stand, chase D while chasing.
  - **Weapon choice:** only owned weapons count.
- **Self-kill reset** (`bot_defenderReset`, `--defender-reset`).
  - **When:** the own flag is home, and the runner is dead or over 2,500 u
    from the stand.
  - **Condition:** the defender is over `bot_defenderResetDist` (1,800 u)
    from its home, or over 1,000 u away and missing a weapon with nothing to
    grab.
  - **Benefit test:** the team's respawn points (`team_CTF_<team>spawn`, one
    chosen at random) must average under 60% of that distance to its home.
    Kejim's red respawns spread over the red half (mean 1,401 u from the
    chaser's post).
  - **Limits:** at most one reset every 10 s, and none within 5 s of a spawn.
    `G_Kill`, as `/kill` does.
- **Proximity mines** (`bot_defenderMines N`, `--defender-mines N`; base D
  only).
  - **Spots:** `posts.json` `mine_spots`, three per stand at 186-323 u. These
    are the runner's approach corridors: k-means over its samples 120-380 u
    from the stand before the pickup. The owner laid his six at 100-409 u.
  - **When:** it keeps a live own mine on up to N spots while its flag is home
    and the runner is over 1,200 u from the stand.
  - **How:** walk onto the spot, look down (pitch 80) and alt-fire once. The
    mine arms in 2 s, fires on an enemy within 128 u, and blows up by itself
    after 30 s, so it is re-laid. A `MINE` line is logged when the ammo drops.
  - **Supply:** full trip-mine ammo is granted at the release, as the loadout
    is; after a death it rearms them.
- **Ambush** (`bot_chaserAmbush`, `--chaser-ambush`; chase D).
  - **Posts:** `posts.json` `ambush_posts`, where the owner stood for the
    throws that put the runner into the chasm; blue by the map's point
    symmetry.
  - **Choice:** while its flag is home, the post farthest from the own stand
    that the runner has not yet passed and the chaser reaches first (straight
    line x 1.3 at 450 u/s plus 0.3 s, against the runner at 500 u/s or more).
    With none, its own post.
  - **Holding:** it holds the post until the runner is 500 u past it. The
    lethal-throw rule does the throwing.
- **Tools.**
  - `movement_bench.py` options set these cvars only when switched on, so an
    arm without them is configured exactly as before (a test checks this).
  - `tools/defense_laps.py` runs a laps world: the runner laps Kejim against
    both defenders at 5x (25 ms game step) with the roles campaign's
    settings, for a fixed stretch of game time, many lives on both sides.

## Measured

Builds `learn-build-20260929-a..e`; worlds are in
`.codex-research/learn-20260929-a/`.

**The first laps worlds found three faults, fixed in build e.**
- **Rearming:** the chaser walked to rocket launchers 3,300 u away (a weapon
  pickup gives only a few rockets, so it stayed "low"). Fixed by the
  three-shot rule and the home radius.
- **Resets:** the chaser reset 23-25 times in ten minutes. Distance was
  measured from its new ambush post before it had walked there, and the
  respawn rarely put it nearer. It also fell 5-7 times walking back from
  spawns, against 1 in the control. Fixed by measuring from its current home
  and adding the respawn-point test: falls dropped to 2 per world.
- **Ambush:** chase D left each ambush post just as the runner reached it.
  Fixed by the 500 u hold.

**Bench, everything off, against `counter-20260929-a`** (same settings, build
`counter-build-20260929-c`):
- Returns, class 150: 18 of 18 identical.
- Pickups, class 150: 22 of 24 identical, with the same outcomes. The 2
  differ in time only (25 and 450 ms), both on Kejim L1 stand starts, which
  vary between runs.

**Bench, all four on against all off, both defenders** (first encounter:
defenders armed and mines fresh at the release):

| Arm | Carriers killed | Stopped before the grab | Runner deaths (off → on) |
| --- | --- | --- | --- |
| Pickups class 150 | 6 → 10 | 4 → 5 | falls 10 → 10; mines 0 → 2; rockets 1 → 4 |
| Pickups class 50 | 4 → 14 | 1 → 1 | falls 5 → 3; mines 0 → 4; rockets 1 → 9 |
| Returns class 150 | 2 → 4 | 0 → 0 | mines 0 → 1 |
| Returns class 50 | 2 → 3 | 0 → 0 | mines 0 → 4 |

**Laps, 10 game minutes each, both defenders:**

| Worlds | Captures | Runner deaths | Chase D deaths | Notes |
| --- | --- | --- | --- | --- |
| Off, builds c and e (4) | 16, 13, 14, 16 | 6, 14, 13, 11 | 1, 1, 1, 6 | |
| On, build e (4) | 16, 14, 16, 16 | 7, 13, 11, 9 | 18, 19, 18, 20 (16-18 resets) | mines kill 3-5 a world; rearm pickups 21-29; mines laid 39-43 |
| On, mode 2 throws (2) | 16, 16 | 9, 9 | 21, 20 | fewer throws (27-35 against 50-53), same kills |
| Rockets + repeater alt (`8a,11`), off (2) | 18, 14 | 6, 0 | | 3-5 shots in ten minutes |
| Rockets + repeater alt, on (2) | 18, 18 | 6, 8 | | base D fell 5 and 10 times rearming |

## Reading

- **What the behaviours do.** They behave as intended: the defenders rearm,
  reset only when it helps, keep mines on the approaches, and meet the runner
  at the chasm. On the bench's first encounter they kill more carriers,
  mostly with rockets and mines.
- **What they do not do.** Over many lives they do not cut captures, which
  stay at 13-18 per ten minutes in every configuration. The owner held the
  runner to 1 capture from 5 pickups in 3.6 minutes.
- **The gaps that remain:**
  - **Gunfire.** The owner fired 54 shots in 3.6 minutes. The defenders fire
    3-32 in ten minutes whatever the loadout: the aim gate (`bot_aimGate 2`,
    style 2) holds most splash shots at a hopping runner. That is the largest
    difference left.
  - **Escapable throws.** From the ambush posts chase D throws more (17-30
    lethal throws a world against 11-26) but kills fewer (3-7 pits against
    4-11). The runner steers in the air back to a ledge, as in the
    [one-defender pilot](2026-09-27-one-defender.md). Mode 2 throws only
    inescapable ones: fewer throws, the same kills.
  - **Falls while rearming.** Rearming walks bots to items near the pits.
    With the repeater loadout, base D fell 5 and 10 times in ten minutes.

## Second round: the three gaps (builds f and g)

Owner: "try to address those 3 issues".

**1. Gunfire.** The diagnosis above was wrong: the aim gate held only 0-4%
of the pre-pickup frames. The per-frame no-fire reasons (`PRESHOT`) were
runner unseen 51-56%, out of range 21-27%, and `weapon` 8-22%. `weapon` is
most of the frames where the runner was seen and in range.
- **The real cause.** The defenders' view demos showed they were unarmed.
  - In the "off" laps worlds, base D owned its rocket 20% of its alive time
    and chase D 2%. The fixture grants the rocket once, and after the first
    death they carried only the spawn pistol, outside the loadout.
  - With rearming on, base D still could not re-arm: Kejim has no rocket
    launcher within 1,500 u of the red stand. It held the trip mine 90% of
    the time.
- **The fix.** "Armed" is now any one loadout weapon with three shots.
  Rearming goes for the nearest item that gives any of them. Once armed, a
  defender still takes one it lacks within 600 u. The laps worlds use the
  owner's weapons: `bot_defenderWeapons=8a,11,10a` (repeater alt, rocket,
  flechette alt).
- **Result.** Base D holds a loadout gun 69-98% of its alive time (off: 4-5%)
  and chase D 65-95% (off: 22-30%). Fires per ten minutes rose from 0-2 to
  33-64 for base D and 10-24 for chase D. That is still well under the
  owner's rate.

**2. Escapable throws.** Two findings from pairing throws with outcomes
across all retained worlds:
- Throws with 0 escapes killed 65% of the time (17 throws). With 1-7
  escapes, 7-35%.
- Throws from 400-600 u killed 38%, against 10-19% at other ranges. The
  owner's median throw range was 458 u.

`bot_defenderPatience` (`--defender-patience`) holds an escapable lethal
throw while the same throw two frames on (runner ballistic) is lethal with
fewer escapes. Build f laps: 106 lethal throws for 24 kills (23%) against 138
for 28 (20%) without it. Fewer throws, slightly better per throw; the runner
almost always keeps an escape from where the defenders stand.

**3. Falls.**
- **Item-level fixes, no gain.** Build f charges a lethal fall within 12 s of
  choosing an item to that item (skipped for the rest of the game), and
  prices a drop at 3 u of walk per unit. Falls did not fall: base D 3-12,
  chase D 2-9 per world. They are not about items.
- **The real cause.** Almost every fall takes off from the walkway between
  the red base and mid, (130-470, -620 to -90), z 1,808, next to the chasm.
  Both defenders now cross it often. Every goal change (item, mine spot,
  post, ambush) restarts the movement search (`Move_Reset` clears its avoid
  list), and the reset at respawn added in build a wiped it too.
- **Build g** removes that reset and adds `bot_defenderWalkErrands`
  (`--defender-walk`): hop searches only while chasing, errands walked.
  - Falls dropped to base D 2-8 and chase D 1-5 per world. The control is
    1-3 and 1-6.
  - Base D's remaining falls are errands (38 in eight worlds), mostly the
    walk between its guard posts on the way back from respawn points across
    the walkway. The walking planner steps off that walkway. That is a
    movement fault, not a defender decision, and it is left open.

**Laps, build g, all behaviours, the owner's loadout, errands walked**
(10 game minutes each):

| Worlds | Captures | Runner deaths | Base D falls | Chase D falls |
| --- | --- | --- | --- | --- |
| Walk (4) | 10 (7.2 min), 16, 16, 14 | 11, 11, 10, 15 | 2, 8, 8, 2 | 1, 5, 3, 5 |
| Walk + patience (4) | 17, 14, 14, 14 | 7, 12, 12, 14 | 8, 4, 4, 5 | 5, 4, 4, 5 |
| Off, builds c, e, f (6) | 16, 13, 14, 16, 17, 15 | 6, 14, 13, 11, 7, 7 | 1-2 | 1-6 |

- **Runner deaths** are up about 20% (a mean of 11.5 against 9.7).
- **Captures** are unchanged: a mean of 14.8 against 15.2 per ten minutes.
- **Why captures hold.** The runner loses a life before the pickup and comes
  straight back. Carrier kills stay near 5 in ten minutes. The owner held it
  to 1 capture in 3.6 minutes.

**Identity on build g.**
- No defender, class 150: 24 of 24 identical to `counter-20260929-a`. The
  change in `bot2_move.c` is gated to defenders.
- Both defenders with every behaviour off: 22 of 24, the same two Kejim L1
  stand starts that differ between any two runs (they alternate between the
  same two times).

## Limits

- **Sample size:** one map (Kejim), with 2-4 laps worlds per configuration.
  Laps worlds are unpinned and vary widely: runner deaths ranged from 0 to
  14 in the off worlds.
- **Throw pairing:** throws are paired with their outcomes by log order, and
  the two defenders' lines interleave. Those counts are approximate.
- **Map data:** the mine spots and ambush posts are Kejim-only fixture data.
- **Cheating:** mine laying uses the runner's true distance from the stand,
  not a perceived one.
