# Goal-chain uptime: Kejim item route and dash2, 2026-09-23

Owner test before signing off level 1: how much of a trip is strafing,
walking and standing when the bot is led through several goals, and on a
straight race map.

## Mechanism added

- `bot_goalChain "x y z;..."` (`ai_bot2_objective.c`): the runner's
  objective is each point in turn, advancing within 40 units (72 vertical),
  then its native CTF objective. The chain restarts at `bot_corner_start`.
  Each point prints `[GOALCHAIN] ... event=reached`. When the cvar is empty,
  nothing changes.
- `addrunner` accepts any CTF map when a chain is declared.
- Bench:
  - a `chain` leg with `chain` points;
  - a `reach` pass condition;
  - the map is taken from the case file (`--map` overrides it).
- Cases:
  - `tools/fixtures/movement/chains/kejim_items.json`;
  - `tools/fixtures/movement/chains/dash2.json`.
- `tools/movement_uptime.py` classifies each 25 ms `[MOMENTUM]` sample:
  - **strafing:** airborne, or a grounded run of at most 50 ms between
    flights;
  - **standing:** any other grounded sample under 20 u/s;
  - **walking:** the rest.

## Runs

Build `chain-build-d6bf930b`, reference-cell settings, level 1, Force 0,
with the seed graph on Kejim and no graph on dash2. There are four worlds
per case (`.codex-research/chain-smoke-20260923-a`,
`.codex-research/chain-reps-20260923-a`).

**Kejim, red.** The route is our repeater, our disruptor, our thermal, the
enemy repeater, then the enemy flag and the capture.
- 60.925 s in all three repetitions; the smoke world took 63.6 s.
- Strafing 67.8%, walking 32.1%, standing 0.1%.
- Mean speed 333 u/s, maximum 655 u/s.
- The first stop, 3.3 s from the stand, is 83% walking. The later legs are
  61 to 91% strafing.
- It reaches every stop at only 120 to 150 u/s, because the planner brakes
  into its goal. Nothing tells it the next goal.
- Walking comes in 27 stretches longer than 50 ms, 19.6 s in total. The
  longest is 4.2 s, on the way to the thermal.
- The enemy repeater logs no pickup; the bot already carried a repeater.

**dash2, one lane, 31,000 units.**
- 46.70 s three times and 46.875 s once.
- Strafing 97%, walking 3%, standing 0.1%.
- Mean speed 664 u/s. Speed plateaus at 680-740 u/s from about 10 s on.

## Finding: the landing frame caps top speed

On dash2, each hop at plateau lasts 32 frames in the air and gains about
57 u/s. The single grounded frame then gives the same 57 u/s back. Landing
happens partway through a command frame. The jump is pressed only in the
next command, so the remaining 8 ms substeps of that frame pay ground
friction (6 x speed x 0.008 each).

Air gain per hop falls as about 1/v, and the landing loss grows with v, so
they balance near 700 u/s. PHYSICS.md's model has no such ceiling for a
jump accepted on the landing substep.

Candidate repair, not yet made: issue the jump in the command in which the
forecast predicts the landing. Held-jump rules (`PMF_JUMP_HELD`, Force
Jump) have to be checked first.

## Follow-up the same day: landing jump, follow-through, flight mode

The builds are `landjump-build-3bfdc7bc` and `flight-build-9793f9c3`. The
worlds are under `.codex-research/landjump-20260923-a`,
`force-pool-20260923-a`, `flight-20260923-b` and `flight-sweep-20260923-a`.

- **Landing jump (`bot_moveLandingJump`).** A flight whose next hop is a
  zero-run-up tap presses the tap in the frame where a one-frame trace
  predicts the landing.
  - On dash2 every landing then keeps its speed (before, each lost 7.7%).
    The time falls from 46.7 to 32.7 s, and the top speed rises from 742 to
    1,317 u/s.
  - Round trips from the four respawn starts: mean 33.7 s, against 36.3 s
    without it.
  - Floor: 36 of 36 pass. Level 1 is 8.4 s faster in total (10 of 12 cases
    faster); levels 2 and 3 are flat.
  - **Two deaths at start class 175** (`blue_round_L1_respawn4`,
    `red_round_L1_respawn5`). Both reproduce exactly, and both pass with the
    landing jump off.
  - Both deaths are the same known level-1 defect. After a landing on the
    upper ledge near the red base (about 440, 50, z 1808), the planned
    strafe crossing (edges 87013 and 18180, tap=1) is refused or stale. The
    walk brakes twice for `floor_missing` at 50-130 u/s, walks off the lip
    anyway, and falls into the pit.
  - This is the lip the 2026-09-22 seed-deaths record said the hold was
    never tested at. The landing jump reaches it; it does not cause it.
    Blocks adopting the landing jump.
- **Follow-through (`bot_moveFollowThrough`, no objective brake).**
  - Round trips: mean 36.7 s against 36.3 s without it. The enemy flag is
    a turnaround, so running through it carries speed the wrong way.
  - Floor: level 1 is 10.9 s faster and level 3 is 6.1 s faster, where
    cases end at the pickup; level 2 is 4.1 s slower. It pays only when the
    next goal is ahead.
- **Force pool at Force Jump 3.**
  - Every takeoff is 302 u/s vertical at 0 Force and with a full pool. A
    full pool never drains, so no tap or pre-press engages Force Jump.
  - Round-trip times fall on the same set of values in both arms.
  - dash2 differs by 2 s because decisions diverge at the second commit (an
    aim 0.4 degrees apart). The search reads the Force value somewhere even
    at level 1; open.
- **Air strafing and the 25 ms command.** Aiming once per 25 ms frame gets
  99.2% of the per-substep optimum at every speed; the cadence is not the
  limit.
  - The old flight controller reaches 32-52% of that optimum. It tracks a
    velocity exactly on the heading, which it can only hold by alternating
    opposite key pairs (forward+right, back+left) with the view parked
    across travel.
- **Flight mode (`bot_moveFlightMode`, optional `bot_moveFlightSpeed`).**
  The flight holds one side's forward diagonal and turns the view toward the
  speed-best angle, computed rather than searched. It changes side when the
  travel drifts 4 degrees past the heading, at the existing 360 deg/s turn
  limit. Flights more than 20 degrees off their heading are tracked as
  before.
  - Air efficiency rises to 73-83% of the optimum. dash2 takes 29.1 s.
  - Kejim, 8 pinned start classes, each arm with the landing jump on:
    - round trips: flight mode 1 is 1.6 s slower on average (19 of 26
      pairs slower), and gated at 500 u/s it is 0.8 s slower (15 of 23);
    - item route: +4.6 s and +0.3 s;
    - the flight weaves about the heading and lands differently, which
      reroutes Kejim trips; one item-route world went over a jump pad onto
      the upper level.
  - It stays opt-in: it wins on long straights, not on Kejim.

## Lip guard, the same day

Build `lip-build-9ce428af`; worlds under `.codex-research/lip-20260923-a`
and `lip-sweep-20260923-a`.

- **Cause of the two class-175 deaths.** The momentum brake looked ahead
  `speed x 0.1 s` along the velocity (10-13 units at 100-130 u/s) and did
  not run below 100 u/s. A floor trace with the player's box finds floor
  while any of the box is over the lip, so the brake saw the drop on one
  frame and lost it the next. The walk then resumed toward its target on
  the far side of the refused crossing (`bot_moveWalkTrace` shows the
  target 50-120 units across edge 87013) and slid off.
- **Fix (`bot_moveLipGuard`, default on).** Above 20 u/s the momentum
  chord is at least 32 units long: a box width and a margin.
- **Evidence.**
  - On this build with the guard off, both class-175 worlds die at the
    same times as before (35,975 and 18,650 ms) in every repetition. With
    it on, both pass (34.1 and 33.9 s).
  - The guard also acts inside rollouts, so decisions change upstream. In
    the passing worlds the bot hops over that ledge rather than being held
    at the lip.
  - A direct start at the lip (`chains/kejim_lip.json`) does not reproduce
    the refused-crossing state. From rest the bot hops the crossing; with
    hops off, the path does not route across it.
- **Sweep, guard on, 8 pinned classes x 4 round trips.**
  - No deaths in 60 worlds.
  - Landing jump on vs off: 19 of 29 pairs faster, mean -0.97 s.
- **Floors, guard on: 36 of 36 with the landing jump off and on.** Totals
  against the 2026-09-22 floor, one run per case:

  | Arm | L1 | L2 | L3 |
  |---|---|---|---|
  | Guard alone | 223.1 -> 212.8 s | 130.8 -> 135.9 s (10 of 12 slower) | 137.9 -> 138.4 s |
  | Guard + landing jump | 213.1 s | 133.3 s | 140.2 s |

  Level 2 slower on 10 of 12 cases has now appeared twice (under
  follow-through, and under the guard alone); it is unexplained.

Owner ruling, 2026-09-23: `bot_moveLandingJump` defaults to on.
