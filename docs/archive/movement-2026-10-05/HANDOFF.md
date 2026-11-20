# Current state (2026-10-05)

Replaced in place each session; this head stands alone. Session blocks below are history, newest first; the roadmap
and status log are in PLAN.md ([roadmap](../../movement/PLAN.md#roadmap-owner-2026-09-27), [status log](PLAN.md#status-log)).

- **Branch and build:** `movement/level1-gate`; latest build `walkdefault-build-20261005-a` (`bot_moveSearchAlone` 2048, `bot_moveWalkDefer` 6, `bot_moveWalkPool` 12, `bot_moveWalkRescue` 1, `bot_moveDropDefer` 6, `bot_moveClimbBound` 1 by default; `bot_forecastBodyMs` off); latest build `drop-build-20261005-b`.
- **Milestone B** ([MILESTONE_B.md](../../movement/MILESTONE_B.md)): offense, camper, base, chase a team, 2v2 first, then the 4v4.
  B1 (camper) and B2 (two-sided 2v2: offense + chase, owner) done; **B3, the 4v4, runs** with
  `defense_laps.py --two-sided --camper --roles base,chase` on Kejim (~8.8 captures a world) and Dantooine v3
  (`--map ctf_dantooine_v3`, ~9.3), red/blue balanced; standoffs scored by `tools/laps_standoff.py`.
  Records: [camper](../../movement/records/2026-10-02-camper-b1a.md), [B1 merge and testing](../../movement/records/2026-10-02-b1-merge-and-testing.md),
  [2v2/4v4 structure](../../movement/records/2026-10-02-b2-structure.md), [base support](../../movement/records/2026-10-02-base-support.md),
  [4v4 stalls and runner camping](../../movement/records/2026-10-02-b3-stalls.md).
- **Defaults added this milestone:** camper protect on; defenders keep out of the enemy flag (goal, route, stand) and
  run a picked-up enemy flag home; unarmed defenders and campers fire the pistol's charged alt (not at an active saber)
  else melee with the saber; empty weapons are switched at once; base D and the camper target an enemy carrier only
  when perceived; the runner camps the enemy stand while a teammate carries and may ask a spent carrier to /kill.
  Off: camper displacement throw, runner hand-off self-kill, base support (built, no capture gain).
- **Testing** (owner: cheaper than eight worlds; at most **four** native worlds at once, AGENTS.md): `defense_laps.py
  --rate 25` waits on the game clock (a 4v4 world ~3-4 min wall at four at once); `tools/laps_campaign.py` runs arms in
  rounds and stops when `tools/laps_compare.py` decides; `tools/laps_scenarios.py` replays recorded moments; `--seed N`
  worlds replay exactly on Dantooine (`tools/laps_identity.py` checks two full-length runs, exit 2 if either is short); Kejim's do not across runs, and a
  stall during setup breaks any (records/2026-10-03-perf-lossless.md). Stall checks: `.codex-research/b3-20261002-a/stalls/frozen.py` and `whyfrozen.py`.

## The tail: what it is made of (2026-10-04)

[Record](../../movement/records/2026-10-04-tail-composition.md). Retained-evidence study of the slow bot-frames (over 25 ms) on the
P2 default: ~76% straight searches (58% that commit, 18% that find nothing), ~11% gap-edge, ~6% state-plan hops, ~2%
climbs, ~3% path planning. About four fifths of the slow straight-search time is the 200 ms walking cadence, not a
landing, so no kept speed is at stake. The proposed deferred walking search (the early search with a K-frame horizon)
was checked first: walking predictions 5-8 frames ahead are taken 44% of the time against 38% at 9-12 (diagnostic
tally, `horizon-build-20261004-a`, decision-identical), so it would repeat the early search's trade; not recommended.
Owner's order then: (1) why short predictions fail: candidate refusals were the shared `spikePlans` id (fixed, exact;
served early searches x2.2), the rest is the defender/camper/chaser aim layer changing the command; the tail and p99
still match no early search, so P2 stands. (2) Step profile in the 4v4 (`bot_moveProfile`): Pmove 42-49% (its traces
21-29%), corridor test 15-17%, steering 12-15%, copy/guard 8-12%; the largest exact lever, corridor traces through the
trace cache, is about 5-7%, so step cost cannot be halved exactly. (4) Deferred walking search built
(`bot_moveWalkDefer`, `bot_moveWalkPool`, default off, c9e42a22): K = 6 pool 12 cuts frames over 25 ms 32-43% and p99
52-51 -> 41 ms (mean +11-25%), but team captures fall on both maps (-1.1, undecided) and camper captures (Kejim -1.3,
2.3 se): aiming roles' takes are refused 45-60% and each refusal delays the hop 150 ms. Owner chose the rescue
(`bot_moveWalkRescue`, default 1 with deferral): K = 6 pool 12 + rescue gives p99 56.5 -> 34 ms, frames over 25 ms
1,550-1,720 -> 600-670, mean 6.0 -> 5.1-5.5 ms; outcomes within spread on 16 paired worlds a map (team captures -0.3,
-0.25), Kejim runners' falls +1.5 +- 0.9. **Default on (owner, 2026-10-05):** K = 6, pool 12, rescue on
(`walkdefault-build-20261005-a`). Not re-measured on it: solo bench, floors and gates, Kejim falls. **Target (owner,
2026-10-05): 4v4 p99 under 25 ms.** By size on this default: solo p99 19-20 ms, 2v2 24.5-26, 3v3 26, 4v4 34 (604-671
frames over 25 ms; the target allows about 246). Of the slow 4v4 frames left, planned drop searches are about 55% (44%
fail: candidates land off the route), landing straight 17%, climbs 8%, whole walking/state searches 13%. **Next
(owner):** record options - defer drop/edge searches, exact climb bound, why drops fail. Done (dd97cdae): climb bound
default on (exact); drop deferral (`bot_moveDropDefer`, default off) cuts Dantooine frames over 25 ms 656/604 ->
413/352, p99 to 28 ms, but runner captures -1.2 (2 se, more weapon deaths, fewer falls); Kejim untouched (p99 ~37 ms,
69% takeoff-window straight searches that find nothing). **Drop deferral default on (owner, 2026-10-05)** with
no ledge waits (top-3 recipes from the live state, stale results re-deferred next frame): solo bench +0.5%, 36/36;
Dantooine 4v4 team captures -0.42 +- 0.99 (was -1.47); frames over 25 ms -37% in-session. **Next:** Kejim's
takeoff-window searches (p99 ~37 ms there); the unexplained internal bench differences with deferral off.
**Deferral behaviour measured (2026-10-05,** [record](../../movement/records/2026-10-05-deferral-behaviour.md), retained worlds only):
drop deferral leaves no runner ledge pause (standing at drop edges +1.2 +- 1.3 s a world, deaths there unchanged; the
pre-fix arm's extra weapon deaths match its fewer falls, total runner deaths flat). The one ledge stall was a pre-fix
defender standing 7 minutes at a drop edge (`drop-20261005-b/defer/dan-6`), gone with the fix; no frozen worlds in
any arm; solo legs never stand over 0.5 s. **Walk deferral changes style:** every role is airborne 2.6-6.6 points
less (runner -4.6 on both maps, same build) and the runner's ground stretches between hops grow 9-16%, which captures
missed. **Where (option 1, same record):** the lost hops are the walking search's, not the landing's: the cadence
timer is stamped at the deferral's take, so walking decisions come every 400 ms instead of 250 and 150 ms late
(runner cadence hops 145 -> 120 a Dantooine world, ground before each 1.19 -> 1.60 s). **Option 1 built**
(`bot_moveWalkCadenceStart`, `cadence-build-20261005-b`; removed, never committed): cadence and no-hop retry timed from
the deferral's start restore the 250 ms rhythm but not the hops (air share within spread of the default); hops follow
decisions, and the shortfall is late deferrals (~530 a Dantooine world, pool-starved). Not adopted. **Capture metrics
(option 2):** in two-sided worlds `laps_compare`'s `team_caps`/`runner_caps`/`camper_caps` count own-flag touches
(captures + returns; camper = blue only); the Kejim "camper captures -1.73" is -0.43 +- 0.56 on both campers' true
captures, and re-scored on true captures no deferral pair moves beyond 2 se. **Fixed (owner, option 1):** two-sided
`team_caps`/`runner_caps`/`camper_caps`/`pickups` are now true counts, a team, both teams pooled (se about halved);
new `team_returns`; one-sided output byte-identical; `tools/test_laps_compare.py`. Comparison files before 2026-10-05
keep the old meaning. **Flow measured** (12 paired Dantooine seeds, published page): the deferral chains fewer hops
(-22 a world) and runs longer on the ground between them, mostly still moving; takeoff speeds unchanged. **Owner:
not ideal, accepted for the performance gain**; the cadence cvar is removed. **C: filled** by never-removed staging
(226 GB, cleaned; guards in `defense_laps.py`/`play_vs_runner.py`, AGENTS.md). **Next:** Kejim's takeoff-window
searches (4v4 p99 under 25 ms).

## P1 and P2 built and measured (2026-10-04)

[Record](../../movement/records/2026-10-04-bodyms-and-search-alone.md). Owner: build both. Both default off; off replays the previous
build exactly. **P1** (`bot_forecastBodyMs`, players block rollouts only for the first T ms): body refusals 14% -> 0-7%,
in-flight body contacts +35-75%, steps +1-5%, no capture change on 16 paired worlds a map, Kejim runners' falls +4.7 a
world at T = 0 (4.6 se): stays off. **P2** (`bot_moveSearchAlone`, no early search within R of another player): 4v4 mean
frame cost 6.8-8.5 -> 5.7-6.6 ms toward budget 0's 5.5-5.8, p99 and over-25 ms frames unchanged in every setting; R = 1024
and budget 0 laps arms change no outcome beyond spread. **Done (owner):** P2 default on at R = 2048, confirmed on 16
paired worlds a map (no outcome beyond spread; 0 replays the old default exactly). **Next:** the tail (p99 ~52-56 ms)
still needs whole searches finished outside their frame.

## Early search and player bodies: studies and proposals (2026-10-04)

[Record](../../movement/records/2026-10-04-early-search-studies.md). Solo, the early search serves 78% of straight searches; in the
4v4 19%, and 89% of its misses had no early search at all (not compute). 13% of 4v4 candidate rollouts are refused on
a player body, and in 79% of 38,894 contacts that player had moved on (median 148 u) by the contact time. **Next
(owner, teed up for the next session, independent):** P1 rollouts treat players as obstacles only for the first T ms
(decision quality; may add ~5-10% steps); P2 early search only away from other players (compute; judge on slow frames
too). Full proposals with measures in the record's Proposals; owner picks which to build.

## Early search (`bot_moveSearchBudget`): no tail gain, not exact with other players (2026-10-04)

[Record](../../movement/records/2026-10-04-search-budget.md). The budget serves 18% of straight searches in the 4v4; budgets 8 and
16 raise neither hits nor the tail; budget 0 (no early search: one search, at the landing) has the same p99 and a 15-45% lower mean. Every budget gives a different
game: the pre-search judges candidates against other players where they stood frames before the boundary, and the
boundary checks only the bot's own state (found with `bot_stateTrace` and the new `bot_forecastBodyLog`). Rollouts
now also restore the game's random stream (latent leak, no effect here). **Next (owner):** no early search in play (needs
a laps comparison), make it exact (would rarely hit in a 4v4), or keep it; the tail still needs searches finished
outside their frame.

## Perf counters: the time is in simulated forecast steps (2026-10-04)

[Record](../../movement/records/2026-10-04-perf-counters.md), build `perfctr-build-20261004-a` (`[PERFCTR]` with
`bot_moveFrameMeter 1`; decision-identical, +2-6% frame cost while on). A bot-frame costs ~600 us + 13.5-15 us per
simulated forecast step: steps are ~90% of bot time and 81-85% of over-25 ms bot-frames. Fable's other candidates
measured: guard scope traffic ~8% (the proposed snapshot skip is not exact: 11-12% of eligible scopes saw a changed
world), cvar reads ~3%, near-list rebuilds 0.05%, mover memo, G_Find, sweeps, Reaches each at most 0.3%. **Next
(owner):** replay coverage (32% of rollouts have a matching buffer from another scope; needs a world-equality proof)
or a cheaper step, if mean cost matters; the tail still needs long searches finished outside their frame.

## Review fixes for 3700dbdd (2026-10-04)

[Record](../../movement/records/2026-10-04-review-fixes.md). `tools/laps_identity.py` now refuses short or missing runs (exit 2)
and masks the build stamp. Lossless batch (state-table slot from the hash's top bits, reverse edge index per topology,
strict-aliasing-safe trace hash, no sight trace for kill-feed-dead enemies): identical on Dantooine seeds 1, 4, 8, 12,
frame cost unchanged (seed 2 8.31 -> 8.19 ms, seed 1 6.98 -> 6.96 ms, p99 51.5 ms both). `bot_defenderTheftGoal`
(default 1): an unseen theft points base/solo D at its stand instead of a stale goal (median 914 u off, 0 u on);
16 paired worlds an arm, captures unchanged, defender enemy-flag pickups +0.62 +- 0.26 a world; owner kept it on.
**Next (owner):** the p99 tail still needs long searches finished outside their frame.

## Lossless performance, HUD knowledge, stranded kill (2026-10-03, later)

[Perf record](../../movement/records/2026-10-03-perf-lossless.md), [HUD record](../../movement/records/2026-10-03-hud-knowledge.md). Owner: no quality loss, fix
the hitching; a thread is the nuclear option. Exact speed-ups (graph planners, forecast caches, per-frame lookups),
each verified decision-identical on seeded Dantooine worlds with `tools/laps_identity.py`: mean frame cost -10-15%,
frames over 25 ms -19-24%, p99 still about 46-48 ms (single long searches). Defenders now use the HUD's flag theft
and the kill feed (`bot_defenderTheftKnown`, `bot_chaserInterceptAge`, `bot_runnerKillFeed`); a bot stranded off the
graph /kills after 10 s (`bot_strandedKillMs`). Latest build `perf-build-20261003-c`. **Next (owner):** the remaining
lossless items in the perf record, or a way to finish long searches outside their frame. The audit's knowledge
leaks (sight cone, hearing through walls, dropped-flag positions, teammate Force) are accepted as they are (owner,
2026-10-04): not open items.

## B3 on Dantooine, standoff scored, frame cost, chase D fixed (2026-10-03)

[Record](../../movement/records/2026-10-03-b3-dantooine.md). Owner: "do 1-4". Dantooine 4v4: 9.1 captures a world against Kejim's
7.6, a third of the falls, balanced. Standoffs (both flags off their stands) 47% / 40% of the time, median 7 s,
broken by a carrier's weapon death (68-80%) or fall, a third capped within 20 s; no frozen world in 64.
The frozen `runcamp/off-4` was two deadlocks: chase D on the enemy carrier's head, its splash shot held and its brace
zeroing the step-off (`bot_defenderBraceOnBody`, default 0, fixed; seed-12 replay), and an unarmed chase D that could
not melee: **chase D had run on default Force powers since 2026-09-28** (`kejim_defender.jkb` was LF, the game's
parser needs CRLF: no saber, Force Jump 1); `.gitattributes` now pins `*.jkb` to CRLF. With both fixes captures hold
(+1.2 / +0.2), runners die 3-4 fewer times a world to weapons, chase D melees.
**Frame cost, the open B3 item:** eight bots average 6.5-8.5 ms a frame but miss the 25 ms frame in 5-8% of frames
(p99 41-66 ms, 15 frames over 200 ms in 5 min at 1x). **Next (owner's call):** a per-frame search budget shared by
the bots; whether chase D should act on an unseen pickup (the HUD's flag status; one 171 s standoff on Dantooine).

## Pit kill on (owner), exact replays (2026-10-03)

[Record](../../movement/records/2026-10-03-pit-kill-and-replays.md). `bot_pitKill` (default 1 in play): the native fall kill at the
trigger touch instead of 3 s later. Measured +1.86 +- 0.63 runner falls a world, captures unchanged, mechanism
unexplained; owner keeps it on. Laps worlds now replay exactly with `defense_laps.py --seed N` (fixed seed, `botat`
setup, Ghoul2 clock set per frame in `SV_Frame`); `bot_stateTrace` diffs two runs frame by frame.

## Our 4v4 team against legacy Bot2 (2026-10-03)

[Record](../../movement/records/2026-10-03-vs-legacy.md): `defense_laps.py --vs-legacy 4` (ours blue, four legacy `Legacy<n>_v2` red);
defenders now target any enemy bot. 16 worlds on `legacy-build-20261003-a`: ours 26.8 captures a world, legacy 0, ours
won all 16. The legacy bots self-kill 27 times a world by their own rules.

## Runner travel falls (2026-10-03): body contacts fixed, default on (owner)

Owner: accept camping's cost, then work on runner travel falls. [Record](../../movement/records/2026-10-03-runner-travel-falls.md).
Of 9.4 runner falls a world: thrown 3.6 (pulls on an airborne runner, which cannot counter), shot/knocked 2.4, own 3.4.
A body touched in flight was invisible to the hit test (Pmove clips it) and to the rollouts. `bot_moveContactKnock 1`
voids the hop and recovers: both runners' falls 11.11 -> 8.76 a world (-2.3 +- 1.0, build `contact-build-20261003-a`,
16 worlds an arm), contact-preceded falls down two thirds for every role, captures and pickups unchanged, defenders die
more to weapons (+2.9) and fall less. **Done (owner):** default on. Owner (2026-10-03): no hopping less near pits under a puller; thrown/knocked falls are not to be traded for speed.

## Runner falls while camping: located (2026-10-03), owner accepted the cost

[Record](../../movement/records/2026-10-03-runner-camp-falls.md), offline on the retained `runcamp` worlds (`falls.py`, `exposure.py`
there). The +2.3 was `laps_compare.py` counting the blue runner alone; both runners 8.67 -> 9.86 a world (+1.2 +- 1.2,
not significant). Falls while camping +0.8 +- 0.24: 14 of 16 on the route into the enemy base, at 0.38 a travel minute
against fetch's 0.53 (exposure, not a camping defect); 2 at lower-ledge camp spots beside the pit (base D throw or
shot); none chasing the enemy carrier. `laps_compare.py` now prints `runners_falls` and `runners_weapon_deaths`.
**Options for the owner:** accept the cost; keep camp spots at the stand's level (about -0.1 a world, unmeasurable);
or work on runner travel falls in general (return 0.92 a travel minute, about 40% thrown).

The plan that led here, kept for reference:

Owner (2026-10-03): runner camping is wanted, "a bit of denying the enemy from capping more so than capping
yourself"; keep it, fix its falls. Measured (`.codex-research/b3-20261002-a/runcamp/`, off-1..16 / on-1..16, build j):
runner falls 3.5 -> **5.8 a world** (+2.3 +- 0.8) with camping on (blue runner only, see above); pickups +3.2,
captures unchanged.

1. **Locate the falls first, offline.** From the retained `on-*` worlds: each runner MOD_FALLING death (game log
   `Kill:` lines; Runner_v2 and Runner2_v2), its last `[RUNNER] RUNNER_CAMP` goal before it (camp or fc), the runner's
   last CSV positions and speeds (bot2-play CSV, the runner's rows before the death), and the `[MOVE]`/`[ROUTE]` lines
   (edge kinds: hop, drop, climb, pad, knock). Classify: on the way to the camp spot (route into the enemy base), at
   or near the camp spot, chasing the enemy carrier (`fc`, the goal is a snapped cell near a moving carrier), or
   thrown or knocked (defender THROW/HIT, splash). Compare the same classification on `off-*` so the +2.3 is
   attributed, not guessed. Reanalysis of retained worlds is cheap; do not rerun before this.
2. **Likely suspects, to confirm or drop:** camp spots are interior cells within 160 u of the stand's height
   (`Bot2_CampSpot`, bot2_defender.c `CampSpot`) but nothing checks distance to a pit edge or a lethal drop beside the
   spot; the `fc` goal re-snaps every frame to the cell nearest a moving carrier (no hold like the camper's
   `CAMP_SPOT_MS`), which can drag the runner's hops toward edges; the runner stands still on the spot, an easy pull
   target for base D's lethal throws (`bot_defender 2`); the route into the enemy base is the runner's normal
   strafe route, so falls there would show in the off arm too.
3. **Fix by the cause found,** behind a cvar, keeping the capability (owner rules: fixes keep capability, braces not
   holds; visual effects never steer routes). Measure with `laps_campaign.py` (4v4 common args as in
   `b3-20261002-a/runcamp`), primary `runner_falls`, four worlds at once; check pickups and captures do not drop.

## Other open items

- Done 2026-10-03 ([record](../../movement/records/2026-10-03-b3-dantooine.md)): the frozen `runcamp/off-4` explained and fixed; 4v4
  frame cost measured (p99 41-66 ms against the 25 ms frame).
- One defender enemy-flag pickup a world remains, mostly chase D returning its own flag that lies on the enemy flag;
  carry-home makes it useful. The stand keep-out (`b3-stalls` build c) did not help; keep or remove is the owner's call.
- Base support (`bot_baseSupport`) delivers Force but no captures; revisit once carriers have a reason to need it.

# Update (2026-10-02, evening): milestone B started

- **Branch and defaults:** `movement/level1-gate`. Runner defaults from 872292a8 (build `protectdef-build-20261002-a`);
  `bot_defender 2` from 6f0d9b88, after that build (no build named for it).
- **Passed:** both level gates, last re-taken on the current movement defaults
  ([record](../../movement/records/2026-09-26-gates-current-defaults.md)); wide map compliance on the frozen 14-map list
  ([navmesh off](../../movement/records/2026-09-27-navmesh-off.md), [eight maps](../../movement/records/2026-09-27-eight-maps.md)); milestone A
  (owner, 2026-10-02, [MILESTONE_A.md](../../movement/MILESTONE_A.md)).
- **Runner defaults:** `bot_runnerProtect 1`, reactive only (perceived fire; held up it invites a pull).
  `bot_runnerAbsorb 0`: modes 1 and 2 were measured and rejected, not pending; protect alone ran 6.44 captures and
  20.8 weapon deaths a world against 4.31 / 33.3 with both off, every absorb arm worse
  ([absorb](../../movement/records/2026-10-02-runner-absorb-v1.md), [protect](../../movement/records/2026-10-02-runner-protect-v1.md)).
  `bot_runnerCapKill` on in play, off on the bench ([record](../../movement/records/2026-10-02-runner-capkill.md)).
- **Defender defaults:** `bot_defender 2` (escape-free lethal throws only); human-like aim at range, exact inside
  200 u; base D blocks the runner's route, chase D pursues early, pulls to catch up and returns dropped flags; both
  brace against their own close splash; ambush posts on in laps; `bot_defenderKeepSequence 2`,
  `bot_moveGuestTeammate 1`, `bot_defenderRearmPlan 1`, errands hopped ([status log](PLAN.md#status-log)).
- **Closed:** protect plus a second Speed (50 + 10 of 100 Force, no regeneration while a power is up).
- **Runner Force policy review (2026-10-02), six fixes, fixed, unmeasured:** mine memory keyed on reused entity
  numbers; three knowledge-boundary leaks (protect's damage attribution, the threat model's death detection, the
  exposure test reading live enemy positions and health); threat
  bookkeeping that did not run every frame; static per-client state never reset; the exposure sweep's hurt-volume
  test now conservative. Re-run on build `fix-build-20261002-a` (5de58ce9, `.codex-research/fix-20261002-a/`, same arms
  as `protect-20261002-a/run2.sh`), first 8 worlds an arm: both off 3.88 captures / 34.9 weapon deaths, protect on
  7.12 / 19.1 (old build 4.31 / 33.3 and 6.44 / 20.8 at 16 worlds); protect's gain holds. Found there: the mine
  reason had never fired (0 presses in the old and new arms): the sight trace stopped on the mine's own MASK_SHOT hull,
  and rockets and detpacks were unseen the same way; fixed with the overheal-decay-as-damage fault (the 1 health a
  second above max re-blamed the last attacker) in the follow-up commit, unmeasured: expect more missile and mine presses.
- **Next: milestone B ([MILESTONE_B.md](../../movement/MILESTONE_B.md), owner 2026-10-02):** camper, base support, then a full 4v4;
  2v2 first. B1a done ([record](../../movement/records/2026-10-02-camper-b1a.md), build `camper-build-20261002-c`): `Camper_v2`
  (`adddefender <team> camper`, `defense_laps.py --camper`) takes team captures 7.25 → 12.0 a world on Kejim. Next:
  B1b camping rules, keep-out (on), displacement/camper protect/hand-off (off): [record](../../movement/records/2026-10-02-b1-merge-and-testing.md).
  Testing: `defense_laps.py --rate 25`, `laps_campaign.py`, `laps_compare.py`, `laps_scenarios.py`; eight worlds at once.
  B2: true 2v2 is offense + chase (owner); B3 4v4 runs, balanced; base support built, off (no capture gain).
  4v4 stalls fixed ([record](../../movement/records/2026-10-02-b3-stalls.md), build `b3-build-20261002-i`): 0 frozen worlds in 16;
  pistol and saber fallbacks, carry-home, perceived-carrier targeting. Open: the runner idles while a teammate carries;
  4v4 frame cost unmeasured.
- **Map transfer (2026-10-02, [record](../../movement/records/2026-10-02-map-transfer.md)):** milestone A defaults on ctf4 and
  Dantooine v3, 8 laps worlds each on `maps-build-20261002-a` (e9458384): captures 7.00 and 7.75 a world against
  Kejim's 5.38 on the same build; falls lower than Kejim's on both. The mine sight fix fires (2-3 presses a world). Kejim
  5.38 against 6.90 on the old build is unresolved at 8 worlds.
- **Open candidates (owner's choice):** the defenders' answer to protect (splash, knockback, Force denial; the owner
  tries it in play first); a protect drop-when-clear rule to recover the regeneration a 20 s protect blocks; the
  team situations, deferred until a teammate bot exists, which the
  MoveGuest seam ([record](../../movement/records/2026-09-28-strafing-chaser.md)) makes cheaper.

# Update (2026-10-02, afternoon): milestone A passed

Owner: milestone A is passed ([MILESTONE_A.md](../../movement/MILESTONE_A.md)). Runner defaults: protect on (reactive only - held up
it invites a pull), absorb off, self-kill after a capture in play. Protect and a second Speed cannot both be had (50 +
10 out of 100, no regeneration while a power is up), so that item is closed. `bot_defender` now defaults to 2
(escape-free lethal throws only; `defense_laps.py` follows). The owner will try the defenders' answer to protect
(splash, knockback, Force denial) in play. Next: owner's choice.

# Next session (2026-10-02): milestone A, absorb at pit edges

Read [MILESTONE_A.md](../../movement/MILESTONE_A.md). The runner plays alone against the current defenders; pits are 6% of its deaths
(weapons 94%), throws about 11 hits a world, mostly pulls on an airborne runner. Step 0 gives the runner absorb 3 and
protect 3 (67 of rank 7's 100 points) with an identity check; step 1 is the absorb decision (perceived threats,
forecast exposure, one Force pool with the Speed purchases), measured with new throw replays and laps worlds; step 2
is protect. **Waiting on the owner:** the loadout, whether a defender is assumed to always have a throw, the step 1
acceptance numbers. Self-kill stays off (owner, 2026-10-02: captures too much worse).
Runner self-kill after a capture (`bot_runnerCapKill`, default 0, [record](../../movement/records/2026-10-02-runner-capkill.md)):
capture to next pickup 35 → 16 s median, captures unchanged inside the spread (3.81 / 3.50); default for the owner.
Absorb and protect: activation only (owner); both end on their own after 20 s.
Step 0 met (48/48 identical). Step 1 first measurements ([record](../../movement/records/2026-10-02-runner-absorb-v1.md)): absorb
cuts throw pits 0.62 → 0.12 but captures fall 4.50 → 0.81 (Force: each press stays up 20 s); not adopted, branch
`movement/runner-absorb`.
**Then (same record):** the cause was the forecast refusing rollouts while absorb is up (fixed 947c39e4, no
re-exploration); the owner accepted the remaining trade (deaths -5, captures -1.5 a world): absorb mode 1 is the
default, merged 8fd60ddf. Protect v1 ([record](../../movement/records/2026-10-02-runner-protect-v1.md), branch
`movement/runner-protect`, not merged): up 4% of the time, deferring to absorb 55 times a world; deaths unchanged.
Absorb off / protect on measured (same record): captures 4.31 → 6.44, weapon deaths 33 → 21 a world against both
off; every absorb arm is worse. **Done (owner):** protect on, absorb off by default, merged.
**Next candidates:** pre-emptive protect for a carrier in a fight; the Speed it costs (carrying without Speed
35%); absorb only at real pit edges, if ever.

# Update (2026-10-01, night, last): keep 2 measured, give-up deadline fixed

Read [errand falls library test](../../movement/records/2026-10-01-errand-falls-library-test.md), last section. Merged into
`movement/level1-gate` (defaults 93e2e1b5, build `integ-build-20261001-d`). With `bot_defenderKeepSequence 2`,
`bot_moveGuestTeammate 1`, `bot_defenderRearmPlan 1`: captures back at the control (3.25 / 3.56 against 3.62),
missed landings 142 → 55 walked, falls 0.75 walked / 1.00 hopped against 1.00 for today's walk; races ~0; planner
give-ups 2.6 a world after the deadline fix. **Done (owner):** the three are defaults, merged into `movement/level1-gate`; errands hop.
Self-kill re-tested on them (2026-10-02, same record): still doubles captures (2.81 → 5.69); it fires 20-24
times a defender in ten minutes. Defaults with errands hopped: falls 0.82 a world, captures 2.81. **Next (owner):**
only a much rarer self-kill trigger is worth testing; otherwise leave it off.

# Update (2026-10-01, late night): three hop fixes and a rearm planner measured

Read [errand falls library test](../../movement/records/2026-10-01-errand-falls-library-test.md), last section. On branch
`movement/defender-hops-integ` (af35aea5, build `integ-build-20261001-a`), all default 0: `bot_defenderRearmPlan`
(route time, self-seen respawn timers, teammate claims, waits), `bot_defenderKeepSequence` (defender layer off a
committed hop), `bot_moveGuestTeammate` (guest hops refuse the other guest's predicted path). With all three, missed
landings fall 61-74% and falls halve (hopped errands 2.00 → 1.00 a world, walked 1.37 → 0.69), races go to ~0, but
captures rise 1.0-1.5 a world in every arm with keep. **Next (owner's call):** measure keep 2 (aim in the air) for
the capture cost; scale the rearm give-up with the planned time; then decide defaults and whether errands hop.

# Update (2026-10-01, night): library test for the errand falls

Read [errand falls library test](../../movement/records/2026-10-01-errand-falls-library-test.md). Hopped errands still add about
0.8 falls a world against walked ones (16 laps worlds an arm, build `errtape-build-20261001-a`), but guest flights
already land where their rollouts say (p90 0 u over 4,145 hops), so a tape library adds no fidelity. Half the errand
falls are a goal change resetting a committed sequence in its run-up near the walkway lip (`Move_Plan` holds only
after launch); the same causes 11 of 20 falls with errands walked. **Next (proposed, awaiting owner):** hold goal
changes through the run-up for guests and re-run hop against walk before building the library.
**Run-up hold measured (same record):** `bot_moveGuestRunupHold` removes the errand sequence drops (6 to 0) but errand
falls rise 8 to 11 (hop flights, `no_path` holds) and all falls 26 to 39; not adopted, cvar uncommitted at default 0.
Next levers (owner's call): re-check the next hop after a landing misses its rollout near a lip; brake instead of a
`no_path` hold at speed. Errands stay walked.
**Why hops land off (same record):** an undisturbed defender hop misses 5 times in 3,561; the 122 misses follow the
defender layer overriding a committed sequence (40: 72 u item/flag/mine straight walk, brace, view clamp), the two
defenders colliding mid-hop (36, runner 6), or a run-up yaw offset of one turn step right after the commit (19 of 30,
unexplained). Next (owner's call): keep the defender layer off committed sequences; make defender hops account for
each other; find the commit-frame yaw offset.

# Next session (2026-10-01, night): defender tape library

Read [Golan refit, pedestal mines, jitter](../../movement/records/2026-10-01-golan-refit-mines-jitter.md), last section. Committed:
chase D changes from the owner's 1v1s (escape-free throws via `bot_defender 2`, 2 s reset wait, rearm only on the way,
intercept ahead of the carrier, a human target when no runner bot, `play_vs_runner.py --offense --roles`); the
human-derived self-kill rule is in but off (`bot_defenderSpentReset 0`: harmful in laps worlds).

**Next (owner): a defender tape library** for spawn to weapon to post. Errands are walked
(`bot_defenderWalkErrands`); guests are kept off the runner's decision tapes. Build guest tape support (own index,
parity, fallback), explore the legs offline per spawn point, measure respawn-to-armed-and-posted time and falls
against the walk, then re-test self-kills. Open: make `bot_defender 2` the tools' default; the rocket knock tail.

# Next session (2026-10-01, evening): refit measured, pedestal mines measured, jitter diagnosed

Read [Golan refit, pedestal mines, jitter](../../movement/records/2026-10-01-golan-refit-mines-jitter.md). Uncommitted:
`SelfKnock`'s flechette alt rows 1,050/675/425 to 460/345/200 (single-blast p90, build `refit-build-20261001-a`).
Against the old rows at 16 worlds: kills +2.9 a world, captures -1.4, falls unchanged (base 0.38, chase 0.75).
Pedestal floor mines (west + east, 2) against the current spots: runner mine deaths 3.6 to 6.3 a world, base D's own-mine
deaths 18 to 2; the top mine adds nothing. Jitter: `MoveControl` alternates between +9 and -9 degree views when
its intent velocity is small, not the 200 u aim switch.

**Next (owner's calls 1-3 done).** 1. Commit the refit. 2. Make the m2 pedestal spots the fixture default
(`bot_defenderMines 2`, `bot_defenderMineSpots "80 -1105 1914;215 -1185 1914"`). 3. Done 2026-10-01: refit and pedestal mines committed; `bot_defenderClampHold` (chase D's clamp flips -30%, walking
flips 91 to 72 a minute). The rest of the shake is `MoveControl`'s forward/brake alternation: a defender-only
controller flag needs a runner floor run (owner deferred). 4. The rocket's knock tail: < 80 u p90 620 against a 500 row.

# Next session (2026-10-01, end of day): Golan refit, pedestal mines, aim jitter

Committed today after the knock work: the Golan fix (`g_fixGolanDamage`, default 1, ported from base_enhanced: a
flechette alt direct hit exploded twice in stock code; mode 1 keeps golan jumps off corpses and breakables), the
runner's stuck bail-out (`bot_moveStuckBail`, default 1), `play_vs_runner.py --defenders --team spectator`, and
`tools/mine_placement_study.py`. All in [knock replays](../../movement/records/2026-10-01-knock-replays.md) except the two below.

**Golan fix measured** (`.codex-research/golan-20261001-a/`, build `golan-build-20261001-a`, 8 laps worlds an arm,
0 against 1): the defenders' own flechette knocks at 80-130 u drop from p90 590 to 320 u/s; rocket and repeater
unchanged; falls, kills and captures inside the spread at 8 worlds. `SelfKnock`'s flechette rows (1,050 / 675 / 425)
were fitted to the double blast and are now far too cautious.

**Mine study** (`.codex-research/mines-20261001-a/report.md`, `mines.csv`): three strong base players, 78 demos of 61
base matches, 1,345 mines. 99% proximity, median 77 u from their own stand (pedestal top z 1902 or the floor beside
it z 1890), 2 a session about 100 u apart, re-laid on the way back after a death or while the flag is away; not
chain-free (20% chained). On Kejim, floor mines beside the pedestal are set off by an enemy 33% against 23% on top and
chain half as often (inference: the pedestal blocks the blast). Owner: the pedestal is the choice; where around it is
open. Candidate aim points (red frame): floor west (80, -1105, 1890), floor east (215, -1185, 1890), optional top
(160, -1170, 1902). The bot's current spots are about 400 u out and two of them chain (134 u apart).

**Next, in order.**
1. Golan refit: laps worlds with `bot_defenderKnockSafe 0` and `g_fixGolanDamage 1` to measure single-blast flechette
   knocks at every range (`.codex-research/knock-20261001-a/kspredict.py`; its weapon table is a copy of
   `SelfKnock`, update it with the code), refit the flechette rows, confirm falls and kills on 16 worlds an arm.
2. Pedestal mines: 2 floor (west, east) against 2 floor + top against the current spots, 16 worlds an arm; score mine
   damage on carriers, kills near the stand, captures. Check where the thrown mines actually settle.
3. Aim jitter the owner saw while spectating: suspects are the backpedal clamp alternating view between aim and move,
   or the 200 u exact/human aim switch. Read the owner's session demos
   (`.codex-research/play-vs-runner-20261001T111858Z`, after `play_vs_runner.py --stop` copies them).

# Update (2026-10-01): knock replays reproduce the fall

Read [knock replays](../../movement/records/2026-10-01-knock-replays.md). The 2026-09-30 scenarios (positions only, 1 s before the
fall) gave no fall in 58 worlds: they never recreated the fight. Committed in cef64497: fixture state inputs
(`bot_defenderPostState`/`bot_chaserPostState`, `bot_cornerHealth`, `bot_cornerAir`), `Move_DeclareKnock`, the
`defense_laps.py` pass-through, and `tools/fixtures/movement/defender/knock_scenarios.json` (33 own-splash knock
falls, started at the knock frame with the recorded post-knock velocity). Build `knock-build-20261001-b`.

**Measured.** Starting before the shot with state restored, the defender fires 90% and is knocked 51%, above the
17-44% a close splash shot knocks its shooter in play: the knock is partly chance, so a pre-shot start cannot
reproduce most. Starting at the knock frame: 12 of 28 replays fall (base D 7 of 13, chase D 5 of 15), deterministic,
about 2 minutes for the set (`.codex-research/knock-20261001-a/runknock.py <build> <tag> 1 6`).

**Judged** (record, "Judging the fall fixes"): switching off knock recovery, the guest brake or the lip guard
changes nothing after a knock (the knock's size decides: 9 of 12 knocks of 400 u/s or more fall); switching off
KnockSafe adds 4 falls in 42 before-shot replays. KnockSafe misses the big knocks because `SelfKnock`'s flechette alt
rows are far under the measured p90 (1,050 / 675 / 425 against 600 / 480 / 410).

**Flechette refit measured** (build `knock-build-20261001-c`, 16 laps worlds an arm): defender falls 31 to 16 (base D
0.81 to 0.38 a world, chase D 1.12 to 0.62, target 0.5), runner kills 542 to 552, captures 55 to 50 (inside spread).

Owner kept the refit (committed with the knock fixture). `fit-16`'s five chase D falls are four mechanisms, not one
defect (record, "Chase D's five falls in fit-16"). Both "speed when a plan ends" candidates were measured and
dropped (record, "Speed when a plan ends"): the run-up hold is inside the noise, the brake adds nothing, and together
they doubled chase D's falls; reverted, patch kept in `.codex-research/plan-20261001-a/`. **Next.** Owner's call
between a narrower brake (knock and decline only), the base D hold falls without a knock, or the rocket's knock
tail. The "hold" falls without a knock (most of the
2026-09-30 set) have no reproducing fixture yet. Runner floor not run (inputs unset outside scenarios).

Older session blocks (2026-09-30 and earlier) moved verbatim to [records/handoff-archive-2026-09.md](../../movement/records/handoff-archive-2026-09.md).
