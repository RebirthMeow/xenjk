# Rewrite plan and acceptance

## Roadmap (owner, 2026-09-27)

Both gates are met on the current defaults (`gate-20260926-c`,
[record](../../movement/records/2026-09-26-strafe-skill.md)). The owner set the order of the
work that follows, revised the same day after an outside review: bounded map
list, a pilot before scale, observation kept apart from inference, and one
end-to-end milestone before building capabilities in breadth. Criteria past
milestone A are not fixed in advance; the pilot and milestone A produce them.
Level 3 (held jumps, Phase 3 step 3) moves to the **end**: it is one more
bidder for a Force pool that the CTF brain will already be budgeting.

1. **Wide map compliance** ([Phase 4](#phase-4-second-map), widened).
   - **Frozen list, 14 maps.** Already with fixtures: kejim, dantooine_v3,
     ctf4, taloraan, carratos, metalorn_v2. New: talay, illimiran_v2, telti,
     nelvaan, talravin, manaan, kharzet_v3, kothis_v5 (the versions most
     played in the local demos: kharzet_v3 73 against 46 for the original,
     kothis_v5 68 against 27; metalorn is covered by v2). Additions need the
     owner.
   - **Acceptance per map:** the level-1 and level-2 case shape from
     automatic preparation, with and without the map's navmesh; every case
     completes with zero deaths, owner `move`, no forbidden spend, parity 0;
     **and survival after the objective** (`movement_bench.py --linger`):
     the runner neither dies nor strands after pickup or capture, the
     failures the 2026-09-26 play sessions showed.
   - **Navmesh:** nothing in the repo generates `maps/<map>.navmesh`
     ([LESSONS](../../movement/LESSONS.md#navmesh-from-code-and-the-owner)). Owner: rule it
     out early if possible, then ditch it. **Measured 2026-09-27
     ([record](../../movement/records/2026-09-27-navmesh-off.md)):** the gates used it; it
     is the graph's only source of pad, drop and jump edges. With it off
     (`bot_navmeshOff 1`) Kejim is identical and ctf4 passes (+22.1 s), but
     Dantooine v3 and Metalorn v2 fail all six stand cases (the stand is an
     island without a drop) and Metalorn respawn4 walks 17-29 s longer.
     **Native edges, same day:** `bot_moveNativeEdges 1` (test) finds drops,
     jumps and pads from the world. Mesh off: 78/78 on the four maps,
     Kejim -13.9 s, ctf4 -16.0 s, Dantooine -5.0 s, Metalorn +2.6 s against
     the mesh arm. Native pads (arrival-steered, no deaths) take Dantooine to
     -14.4 s and Metalorn to +13.5 s: the planner prices walking at
     wishspeed while the runner strafes at 550-700 u/s, so an accurately
     priced pad is over-valued; no flat margin fixes both. **Walk pricing
     ([record](../../movement/records/2026-09-27-navmesh-off.md#walk-pricing-hop-law-refuted-the-price-is-double-the-time-same-day)):**
     pricing open straights by the ideal strafe law is refuted (Kejim +17 to
     +23 s, detours across open floor); the planner's walk price is about
     double the measured time, partly the clearance penalty, which a pad's
     landing never pays. **Re-scoped (owner, 2026-09-27):** native drops and
     jumps already met the navmesh record's acceptance; pads are kept and
     priced by the mesh's own formula (length at 250 u/s plus 2,000 ms), and
     link-price calibration is parked until milestone A needs route times.
     **Done, build `native-build-20260927-k`
     ([record](../../movement/records/2026-09-27-navmesh-off.md#native-edges-by-default-pads-at-the-meshs-price-same-day-owner)):**
     native edges default on, pads at the mesh's price, dropped flags by
     graph path, unplanned pads flown to native landings; mesh not loaded,
     78/78 with no deaths after the objective, -31.4 s net against the mesh.
     **Eight new maps ([record](../../movement/records/2026-09-27-eight-maps.md)):** 828 of
     828 trips complete with zero deaths, with and without each mesh;
     Talravin and Talay lose 9.3 and 2.1 s a case to native drops the drop
     search cannot fly (the mesh's drops do not); prepared seeds recover it
     (Talravin -5.7, Talay -2.3 s a case) and are now part of map
     preparation (below). The coarse map model the brain needs
     (zones, chokepoints, carrier escape routes) is a preparation product;
     derive it from the graph first.
   - **Map preparation (owner, 2026-09-27).** For a new map: (1) cases from
     the BSP (`tools/movement_cases_from_bsp.py`, heights checked against one
     engine world with `--graph-world`); (2) the level-1 and level-2 floors
     with the mesh not loaded at two pinned classes, `--linger 4`; (3) a
     prepared seed from those worlds (`tools/movement_graph_prepare.py
     <campaigns> --map <map> --out <seed dir>/<map>_<checksum>.txt`): the
     built graph without the pads, drops, jumps and climbs that failed and
     never succeeded; (4) a third class seeded against unseeded, which must
     lose no case. Seeds: `.codex-research/mapprep-20260927-a/seeds/`.
     A seed is bound to the graph build: rebuild it when the graph's
     candidate discovery changes.
   - **Known gap, deferred (owner, 2026-09-27): drowning.** The forecast
     accepts submerged movement (`ForecastContents`, bot2_forecast.c) but
     models neither air expiry nor drowning damage, so a low-health bot out
     of air could accept a continuation that drowns it natively (review
     note, same day). Not a current concern; when it is, keep shallow water
     supported and model or conservatively refuse the drowning interval.
2. **Demo position pilot** (offline; runs alongside 1). The labelled corpus
   exists: `tools/human_rush_study.py index` links local pug demos to the
   Discord sheets' `Pos` column (`.codex-research/human-rush/index.csv`,
   2026-09-26: 5,444 demos, 1,443 chase, 216 offense, 107 base, 2,726
   unlinked).
   - **Pilot first:** about 20 matches per position on 2-3 maps from the list.
     Question: can the snapshots answer what we want to know (zone occupancy,
     transition triggers, Force use and its context)? Link more demos only
     for questions the pilot shows are answerable and short of data; more
     demos cannot fix an unobservable event.
   - **Observed apart from inferred.** A Force activation, a position, a
     flag state change are observed. Why it happened is inferred. A reaction
     latency is reported only where the trigger is visible in the POV's own
     snapshots; other players exist only while in the POV's snapshots, and
     demos carry no raw commands.
   - The owner's written position descriptions are hypotheses the numbers
     confirm or correct (location to be found).
   - **Pilot run, 2026-09-27 ([record](../../movement/records/2026-09-27-position-pilot.md)):**
     60 demos (Kejim, Dantooine v3), extraction checked against the stat
     sheets. Answered: team state, enemy state as seen or heard, region
     occupancy by flag state, Force use, pits (37% of enemy kills), selfkill
     purposes, team energize flow, binds and calls, carrier-kill weapons.
     Community discussions read and checked against the demos
     ([record](../../movement/records/2026-09-27-community-discussions.md)), with six
     candidate named situations for milestone A (situations 2 and 6
     corrected by the owner); camper style measured from the sheets.
     **Pilot closed.** Next: write the milestone A plan (Kejim, offense
     runner against a base and a chase; situation model known / last seen
     / unknown; first situations: low-Force carrier retreats and calls TE,
     absorb at pit edges and protect when hurt, spent carrier hands off).
     Open owner decision: defenders scripted to the measured patterns
     (Claude's recommendation) or the existing Bot2 defenders.
   - **Milestone A passed (owner, 2026-10-02): [MILESTONE_A.md](../../movement/MILESTONE_A.md)** - the lone runner against base D and
     chase D, starting with absorb at pit edges; team situations deferred until there is a teammate.
   - **Owner, 2026-09-27: start small instead.** One scripted defender that
     pushes and pulls against the unchanged runner, to see what fast routes
     over pits meet. **Run the same day
     ([record](../../movement/records/2026-09-27-one-defender.md)):** `bot2_defender.c`,
     `movement_bench.py --defender 0|1`. Throws almost never kill (2 pit kills
     in 31 hits, both the carrier leaving the stand): the runner's replanning
     air-steers onto the nearest floor 20-50 u off the passive fall line. The
     defender's **body** does the damage: 9 of 144 defended worlds stall for
     good 52-62 u from it on the approach to the flag, because the forecast
     refuses the body and recovery never goes around (2 with the body alone).
     **Round 2 (owner: both, through or around):** melee through the body
     (`bot_carrierMeleeRange 96`) clears every stall at +1.9-3.2 s; defender
     mode 2 (throw only when no air input escapes) throws twice in 48 worlds
     and kills both times, the fast carrier leaving the blue stand 0.8 s after
     pickup. Next candidates: a defender that moves to meet the runner (human
     base throws reach a median 1,520 u out), and the carrier's exit off the
     stand. **Round 3 (owner: chaser on the return route; runner has no
     absorb):** no carrier died on any return arm; a walking chaser cannot
     keep up. Shadow tails (a perfect thrower 250/500/1,000 ms behind on the
     carrier's track) find 7 inescapable frames in 473 s of carrying, all
     mid-hop on level-1 carries with the tail within 110-380 u: the return
     route is geometrically safe against throws. Owner: a good outcome given
     routes were never planned against throws and the runner has no push,
     pull or absorb. All defender work (builds
     `defender-build-20260927-a..f`) committed in 27a27e79. **Strafing chaser
     (owner, 2026-09-28; [record](../../movement/records/2026-09-28-strafing-chaser.md)):**
     `bot_defenderStrafe` runs the runner's searches as a guest that writes
     nothing into the shared graph and never touches the runner's harness.
     372 outcome worlds all pass; no carrier died on a return; it hops
     (median 320, p90 523 u/s, 65% airborne) but, without Speed, does not
     close on the carrier. The only carrier kills (2 a pickup arm) are the
     stand exit, as with the holding defender. Cost: +0.33 ms a frame; the
     two-bot p99 is 12.4 ms, with 1.0 frames in 800 over 25 ms (control
     0.13). **With Speed (`--defender-speed`, build `-e`):** it buys Speed in
     about 3 chases of 4 and runs a third faster (431 / 702 u/s), and its
     throws were never starved. Returns: still out of range and no carrier
     killed. Pickups: the same 2 stand-exit kills. Cost unchanged. Next
     levers (owner): where the chaser starts or goes (a post on the return
     route, or an intercept at the carrier's base).
3. **Milestone A: take a defended flag and escape usefully.** One end-to-end
   case, built vertically: the situation assessment, Force choices and
   offense behaviour that case needs, and no more.
   - **Knowledge boundary (design rule, fixed now).** The bot's situation
     model separates *known* (perceived this frame), *last seen* (with its
     age) and *unknown*. Policy reads the model, never server state directly;
     `bot_threatReactMs` is the precedent. Demo interpretation and server
     omniscience must not become policy by default.
   - **Scored jointly on survival and flag progress**, so standing still or
     never taking the flag cannot pass.
   - **Force budget by named situations.** One pool (Speed, absorb, protect,
     push/pull, team heal/energize, later held jumps), but priorities come
     from named fixture cases with an expected choice (for example: Speed
     against absorb for a carrier under fire; team heal against keeping 50
     for Speed), taken from the pilot where it can supply them. A general
     allocator grows only as those cases demand.
   - Goals go to the single movement owner through the macro seam
     ([ARCHITECTURE](../../movement/ARCHITECTURE.md): goal point in, executable prefix or
     declared refusal out); no new movement owner. The legacy Bot2 roles
     (`bot_forcerole`) are audited for reusable pieces, not adopted.
4. **Milestone B: a full 4v4 ([MILESTONE_B.md](../../movement/MILESTONE_B.md); owner, 2026-10-02).** Camper logic for the
   offense, support logic for the base, then offense, camper, base and chase a team. 2v2 fixtures first (runner and
   camper against base D and chase D; then offense and base each side), then the 4v4; judged on the measured arms.
   It replaces the chase and base milestones below: the fixture defenders are promoted, not rebuilt.
   Superseded: **Chase and base milestones**, each end-to-end in the same way. **Owner
   decision first:** a minimal combat layer; chase and base largely mean
   killing the carrier or holding the stand, and combat is
   [out of scope](#out-of-scope-until-phase-4-passes) until Phase 4 passes.
5. **Level 3** ([Phase 3 step 3](#phase-3-resource-edges-and-the-state-space-planner)),
   unchanged in content.

[Phase 5](#phase-5-retire-legacy-owners) stays gated on Phase 4 and is not
ordered against steps 2-5.

## Status log

**Defender learnings (2026-09-29, [record](../../movement/records/2026-09-29-defender-learnings.md); owner: update defenders based on learnings):** rearm by pickups and a self-kill reset (both roles), proximity mines on the stand's approaches (base D), and ambush posts at the chasm (chase D), each behind its own cvar, default off. With everything off the bench reproduces the previous build. On the bench's first encounter, carriers killed went from 14 to 31 over four arms. In laps worlds (`tools/defense_laps.py`, 10 game minutes) captures stay at 13-18 in every configuration; the owner allowed 1 in 3.6 minutes. The largest gap left is gunfire (the owner 54 shots in 3.6 minutes, the defenders 3-32 in 10), then escapable throws. Build `learn-build-20260929-e`. Second round (build `learn-build-20260929-g`): the gunfire gap was unarmed defenders, not the aim gate. They lost the loadout at the first death, and Kejim has no rocket near the red stand. With any-of-loadout rearming (the owner's `8a,11,10a`) they hold a gun 65-98% of the time and fire 10-64 shots in ten minutes. Throw patience (`--defender-patience`) gives slightly more kills per throw. Walked errands (`--defender-walk`) cut the falls, but base D still steps off the red walkway while walking between posts, a movement fault left open. Laps runner deaths are up about 20%; captures are unchanged (about 15 per ten minutes). Review fixes ([record](../../movement/records/2026-09-29-defender-review-fixes.md), build `aimfix-build-20260929-a`): build g's rearm crashed on a freed dropped item (3 of 4 laps worlds, and the retained `laps-walk-1`), and aiming turned the defenders' movement sideways. With both fixed, laps worlds run the full ten minutes, and defender falls went from 17 to 2. The walkway falls were most likely the aim fault, not the planner. Captures are unchanged. Gaps against the owner's 1v1 ([record](../../movement/records/2026-09-29-defender-gaps.md), build `gaps-build-20260929-a`): base D's leash is off against a lone attacker (owner). An engage grace, a pull-then-shoot follow-up and a throw-range band are behind cvars. No arm moved captures (14.4-16.1 per ten minutes); stops per grab reached 30% against the owner's 80%, and the throw band's apparent 50% kill rate did not hold once enforced. Defaults since 2026-09-30 (same record): human-like aim (100 ms aim view, flechette under 250 u), exact aim inside 200 u, base D blocks the runner's route, chase D pursues early, pulls to catch up and returns dropped flags; laps captures 8.1 per ten minutes (was 14-15), 54% of grabs stopped against the owner's 80%. With `bot_chaserGoal 1` (chase D runs for the runner's destination stand and only shoots and pulls the runner) captures fell to 1.9 per ten minutes and 83% of grabs were stopped (8 worlds); chase D still falls about 1.5 times per world. Falls and route (2026-09-30, [record](../../movement/records/2026-09-30-defender-falls-and-route.md), builds `falls2-build-20260930-a`/`-b`): the pit re-take was an analysis artifact (0 in the pit), and the 1.9-capture result does not reproduce (valid control 4.0-4.5 per ten minutes, 67-70% stops, 16 worlds). Posts are snapped to the graph; the off-graph goals were the ambush posts at z 1887, now corrected, and reachable they double chase D's falls. The airborne decline hold and the goal latch changed nothing measurable, while a guest lip guard halves chase D's hop falls. Falls stay about 1 a world per defender (acceptance 0.5, not met). The carry diagnostic found chase D unarmed in escaped carries; rearming while chasing (`bot_chaserRearmChase`, off) cut captures 4.2 to 2.5 and raised stops 66% to 79%. Route-ahead and pull-then-close showed nothing. The runner floor is identical; uncommitted. Second attempt (same record, build `falls3-build-20260930-c`): base D mostly knocks itself off with its own close rocket or repeater alt, and chase D's hops launched slow because aiming at a runner behind turned its run backwards (Pmove's 0.75). Defaults now brace against the own splash (the shot is still fired; holding it tripled captures) and keep a moving defender's aim within 89 degrees of its travel on the ground. Over 16 worlds each, chase D's falls halve (18 to 9) and base D's are inside the spread (13 to 10), with captures unchanged (3.4 per ten minutes). Base D chases with no weapon; a rearm during the chase did not help. Owner decisions (same record): L4 on, ambush posts on in laps, the airborne hold and goal latch removed. With the ambush on: a respawn loop fixed (the defender resets its movement at respawn), a knock-safety check for close splash, a guest walk brake at lips, and a guest flight trace that found and fixed two clamp faults (a backwards movement command, landing frames). Falls about 0.7 (chase D) and 0.55-0.6 (base D) a world from 1.1 / 0.8, captures 3.7-4.1 per ten minutes, route stops 3-3.6. Runner floor identical to HEAD. Hop fixes (2026-10-01, [record](../../movement/records/2026-10-01-errand-falls-library-test.md)): a tape library would not have helped; undisturbed defender hops land on their rollout (5 misses in 3,561), the misses were the defender layer overriding a committed hop, the two defenders colliding, and a run-up yaw offset. Defaults now `bot_defenderKeepSequence 2`, `bot_moveGuestTeammate 1` and the rearm planner `bot_defenderRearmPlan 1` (route time, self-seen respawn timers, teammate claims): missed landings 142 to 55, falls 1.00-1.37 to 0.69-0.75 a world with errands walked, captures unchanged, races about 0; errands hop again (falls level with walking).

**Runner push 3 / pull 3 (2026-09-29, [record](../../movement/records/2026-09-29-runner-throw-counters.md); owner):** the runner's bot file had no push or pull; it now has both at 3. The defender's throw forecast models ForceThrow's counter: ground only, one level less while moving, and a countered throw still stops the runner at 16 u/s. No defender: 24 of 24 identical. Both defenders at class 150: counters on 7 of 73 hits (the throws mostly land mid-hop), forecast agrees on 70, outcomes unchanged but for one carrier kill. Build `counter-build-20260929-c`.

**Human defense session (2026-09-29, [record](../../movement/records/2026-09-29-human-defense-session.md); owner's 1v1 against the runner):** 14 runner kills, 1 capture (during setup); 11 kills before the pickup. Six were pushes or pulls on the runner mid-hop over the chasm, four were repeater alt, two trip mines on the stand, two rocket. The owner met the runner forward (median 1,917 u from the stand with the flag home), rearmed by self-kill after most kills (8 `/kill`, back on the stand in a median of about 7 s), and laid six mines on the stand. Our defenders lack rearming, traps, forward interception and mid-hop throws. `tools/play_defense_study.py` reads any stopped play session. Owner to choose what to build next.

**Base D and chase D (2026-09-29, [record](../../movement/records/2026-09-29-base-chase-defenders.md); owner: a basic chase D and base D):** two declared defenders at once (`--defender-roles`). Base D guards and fights the carrier only within 1,500 u of its stand. Chase D holds the 2v2 forward post and after a steal intercepts on the carrier's graph route home. Pickups, carriers killed: base 2, chase 3, both 9 (5 attackers stopped before the grab with the chaser present, against 1). Returns: both 3, base 1, chase 0. The chaser reaches the route ahead but stays about 450 u off and shoots little; its firing position is next. Build `roles-build-20260929-c`, not committed.

**Defender posts (2026-09-29, [record](../../movement/records/2026-09-28-aim-kernel.md#2v2-guard-posts-owner-go-ahead-2026-09-29); owner: a lone 2v2 defender's posts):** the approach view showed the attacker hidden until 1 s before the grab from the stand. 2v2 posts (`--defender-posts 2v2`: entry spot, stand, forward) raise the approach seen from 50 to 80% and first sight from 1.6 to 4.2 s before the grab, and shots before the grab from 10 to 15. Kills barely move: one rocket hit does not kill. Not committed.

**Splash shot selection (2026-09-28, [record](../../movement/records/2026-09-28-aim-kernel.md#splash-weapons-shot-selection-and-weapon-choice-owner-bolt-weapons-ignored-for-now); owner: ignore bolt weapons, do the others):** flight time decides hits; the rule is the reach ratio (runner speed x flight time / splash radius) under 1 (`bot_aimGate 2`). Validated on unseen classes at human error: rocket 29 to 59%, repeater alt 56%, concussion 55%, above the humans' 47%. Weapon choice (`bot_defenderWeapons`, build `aim-build-20260928-i`) prefers the concussion. The full defender with this aim kills 1-4 carriers an arm, against 9-19 with exact fire-at-will aim: the humans' 1.2 s engage delay and the gate cost the first-second shots. Open (owner): engage delay, reach threshold. Not committed.

**Aim kernel (2026-09-28, [record](../../movement/records/2026-09-28-aim-kernel.md), [human aim study](../../movement/records/2026-09-28-human-aim-study.md); owner: humanized aim, competent with the most used weapons):** `bot2_aim.c` solves where to point from the perceived target (lead, gravity arcs, floor bursts, a synced flag) and humanizes the view (pursuit, lag, turn cap, seeded error); the defender uses it (`bot_aimStyle` 0 exact, 1 the humans' process, 2 competent). Humans aim at the target's current centre with 9-19 degree splash errors; copied literally that hits this fast strafing runner 3-7%. A plain lag cannot track a crossing target; pursuit fixed it. Competent style on build `aim-build-20260928-f`: zero error matches or beats humans under 300 u (rocket 90%, conc 98%) but not at mid range (rocket 32 against 53%); human-sized error drops the rocket to 28% overall. Bolt weapons hit the saber-holding runner 0-7%: saber blocks, not misses (`g_missile.c`). Open (owner): a saber-holstered calibration target, the error scale, shot selection, weapon choice. Not committed.

**Base defender (2026-09-28, [record](../../movement/records/2026-09-28-base-defender.md), [demo study](../../movement/records/2026-09-28-base-defender-study.md); owner: position around the flag, shoot, chase, pull, study 4v4 and 2v2 demos):** build `defender-build-20260928-i`. The study (22 Kejim and 18 Dantooine 4v4 base demos, 40 2v2 demos) gave posts, weapons (rocket or repeater alt at 150-500 u, about 47% hits), a far chase, and carrier pulls at 350-700 u at a median range rate of 0 (not a receding trigger); fire-then-pull comes at the refire time (about 1 s) and did not change outcomes in 4v4. The defender now guards the study's posts (`bot_defenderGuard`), shoots a declared weapon with a straight lead (`bot_defenderWeapon`), pulls the carrier in a distance window (`bot_defenderPullCarrier/Max`, optional `PullRate`, `ComboMs`). Rocket, reaction 250 ms, chase with strafe and Speed, Kejim: carrier killed in 9-10 of 36 returns (all in the first 1.2 s of the carry, beside the guard) and 14 (rocket only), 19 (with pulls), 17 (combo) of 48 pickups, against 0 in the controls; hits 26-30% a shot at 450-590 u. The runner has no answer yet: this is milestone A's measured opponent. Not committed.

**Strafing chaser (2026-09-28, [record](../../movement/records/2026-09-28-strafing-chaser.md); owner: go ahead):** build `chaser-build-20260928-c`. `bot_defenderStrafe 1` lets the declared defender run every movement search as a guest (`MoveGuest`, `bot2_move.c`): no learned or merged edge, validation, cost or failure charge on the shared graph (its failures go on its own avoid list), and the runner's parity harness, tape, directives and exploration stay the runner's. Its goal is held until a sighting is `bot_defenderRetarget` (256) away, since each goal change resets movement. New frame meter `bot_moveFrameMeter` (`bot2_movecost.c`; bench `--frame-meter`). Kejim returns and pickups at classes 50/150: 372/372 pass; returns 0 carrier kills in every arm; pickups 2 kills an arm, all the class-150 respawn-5 stand exit, which the holding defender also kills; isolation verified per world (0 chaser learns or parity events, every added edge the runner's). The chaser strafes (320 / 523 u/s median / p90) but stays out of range on the returns: it has no Speed and chases the last-seen point. Frame cost: chaser 0.33 ms mean, two-bot p99 12.4 ms (runner alone 7.75), 1.04 frames over 25 ms per 800 (control 0.13). **With Speed (same day, build `chaser-build-20260928-e`, owner):** `bot_defenderSpeed` gives the defender Speed 3 at its release; identity at class 50 held (36/36), so build c's controls were reused. 168/168 pass; the chaser buys Speed in about 3 chases of 4 (431 / 702 u/s median / p90) and was never starved of a lethal throw; returns 0 carrier kills, still out of range (closest median about 1,150 u); pickups the same 2 stand-exit kills; cost unchanged (chaser 0.34 ms mean, two-bot p99 11.6 ms). Not committed.

**Laps falls (2026-09-28, [record](../../movement/records/2026-09-28-laps-falls.md)):** 16 of the runner's own pit falls in eight laps sessions; every fall on the current builds has one of two causes. **A** (5): `SpeedEdge` buys Speed in the middle of a committed hop sequence (Force regenerating across the cost), so hops admitted at the old wishspeed are flown faster and carry past their floor; after such a purchase committed landings miss by a median 126 u (9 of 18 over 128 u) against 13 u (0 of 2,406) otherwise. Bench legs never buy mid-sequence (0 of 2,893 gate landings), which is why only laps show it. **B** (2): the (160, 192) pad's native edge was checked from the trigger's centre; a Force-6 carrier walking on from its west edge falls short, reproduced at every class by `tools/fixtures/movement/kejim_falls` (`fallB_pad_carrier_force6`, death at 7,400 ms; passes with Force 50 or more). The 09-26 builds' other 8 are not attributable without parity. **Fixed (owner: do those fixes), build `falls-build-20260928-j`:** no Speed purchase while a sequence has hops ahead (A); the pad landing is chosen from the live entry at the throw, and a pad with no reachable landing is avoided before it is touched (B), which also fixed `PadArc` tracing against the bot's own hull. fallB now captures at every class (14.5-14.9 s); four laps worlds had no falls and no landing after a mid-sequence purchase, at unchanged lap pace; gate re-taken, every world identical in time to build b's, cost p99 16.8 / 17.1 ms. Committed in 27a27e79. `tools/movement_laps_falls.py` measures both.

**Failure attribution (2026-09-28, [record](../../movement/records/2026-09-28-failure-attribution.md); owner: a body or rocket is not a wall):** the graph is shared by every bot, and two failures used to switch an edge off for all of them until the map reloaded, so a player standing on a landing could take good routes away for a whole match. Now a failure is charged to its cause: a player, NPC or missile charges nothing and only the failing bot avoids the edge for 3 s; a moving mover switches it off for 5 s; the world as before, except that a validated edge comes back after 30 s. Search failures are attributed exactly (the same search re-run with bodies unlinked); live failures by contact and proximity. A replan also routes around live bodies within 512 u, falling back to none. `bot_moveFailCause` (default 1; 0 is the old behaviour). **Gate re-taken on build `attrib-build-20260928-b`: every pinned world identical to the last gate, rows 1-3 met, floors 36/36 on three maps, cost p99 18.2 / 20.9 ms**; build `-c` (body planning, no-thrash fix) solo floor 36/36 with none of the new paths firing, class-pinned defender A/B 48/48 with identical times, four laps worlds with a still defender: no pins, no edges switched off. Also seen, older than this change: falling deaths in long laps worlds (1-4 per session since 2026-09-26), which single-leg cases never show; open. Committed in 27a27e79.

**Both gates re-taken on the current defaults (2026-09-26, [record](../../movement/records/2026-09-26-gates-current-defaults.md)):** items, planned drops and arrival safety on, build `deferpin-build-20260925-b`, library not re-explored. **Every row of both gates met**, level-1 row 3 strictly at every class. Stand times unchanged to the millisecond (level 1 11,650 / 14,925 / 14,325 / 14,125; level 2 8,150 / 9,100 / 9,875 / 9,575); floors 36/36 on Kejim, ctf4 and Dantooine; level-2 coverage medians 8,700-11,400; cost p99 13.6 ms. 32 minutes, no pin retries.

**Deferred class pins (2026-09-25, [record](../../movement/records/2026-09-25-deferred-pin.md)):** `bot_cornerDefer` (fixture only) starts a pinned `--corner-class` at the first frame of the class instead of the runner's rcon timing, the bench's default now. No world is retired for a missed pin: 95/95 shared pairs identical to the rcon-pinned arm, 108/108 passed, world-seconds halved. `--skip-twins` runs 118 of 216 cases a class (mirror twins identical in 1,913 of 1,916 retained pairs); comparison recipe under [Benchmark gate](#cheap-comparisons-owner-order-2026-09-25).

**Surviving the pickup (2026-09-25, [record](../../movement/records/2026-09-25-arrive-safe.md)):** carriers died after fast pickups (arrivals never judged past the flag; the objective change handed a flight to a no-path walk that kept and built speed). `bot_moveArriveSafe` and `bot_moveWalkKeepPath` (default on, owner): old-settings Dantooine deaths 4 of 7 to 0 of 8; floors 328 pairs, -8.0 s, no deaths in either arm. `movement_bench.py --linger` scores survival after the objective.

**Planned drop search; items on by default (2026-09-25, [record](../../movement/records/2026-09-25-drop-plan.md)):** `bot_forecastItems` defaults on (owner). `bot_moveDropPlan` (default on, owner) chooses the ledge crossing, heading and approach speed from the graph's floor below and scores by landing plus route left: 284 pinned pairs, 63 faster, 20 slower, -69.5 s, no death, Kejim unchanged, cheaper searches. The aim cvar is withdrawn.

**Drop searches aimed and items passed (2026-09-25, superseded, [record](../../movement/records/2026-09-25-drop-aim.md)):** full-speed drop candidates failed from off-line run-ups past short ledges and from item-pickup refusals. `bot_moveDropAim` (test, off) aims one heading at the takeoff; with `bot_forecastItems=1`, 279 pinned pairs on four maps: 114 faster, 47 slower, -136.0 s, no death, Kejim unchanged, parity zero mismatches. Aim alone -11.0 s. Defaults pending owner.

**New maps and late-braked slow drops (2026-09-25, [record](../../movement/records/2026-09-25-new-maps.md)):** Taloraan, Carratos and Metalorn v2, never seen before, 216/216 over two floors with zero deaths and owner `move` throughout. They exposed slow drops braking from the start of the run-up; `bot_moveDropBrake` (96 by default, owner) brakes late: 264 pinned pairs, 27 faster, 9 slower, -28.9 s, no death; Kejim and Dantooine unchanged.

**Gates re-taken on the slow-drop build (2026-09-25, [record](../../movement/records/2026-09-25-gates-slowdrop.md)):** build `dropslow-build-20260924-b`, red-stand library re-explored under the circle run-up and slow drops. **Level 2: every row met** - stand **8,150 / 9,100 / 9,875 / 9,575 ms**, 36/36 coverage (medians 7,625-11,825), zero mismatches, floors 36/36 on three maps, p99 12.7 / 12.8 ms. **Level 1:** rows 1, 2, 4, 5 met (stand 11,650 / 14,925 / 14,325 / 14,125, median 14,225, tail 14,925); row 3 fails strictly at class 75 (one world of six waits two frames, 8.3 against 0.0 percent, same time) - owner ruling.

**Circle run-up (2026-09-24, [record](../../movement/records/2026-09-24-circle-runup.md)):** the run-up never circle-jumped; `bot_moveCircleRunup` (test) lets long run-ups push past the ground cap. Library off both arms: level-2 stand 8,350 / 10,125 / 9,875 / 10,125 ms (from 9,900 / 11,850 / 9,725 / 11,550), level 1 -1,850 ms summed; floors 36/36 on three maps. **Default on (owner);** the library re-explore is deferred, so the production library is stale under it and the gates are not re-measured.

**Gates re-taken on the own-flag build (2026-09-24, [record](../../movement/records/2026-09-24-gates-retake.md)):** build `ownflag-build-20260924-c`. **Level 2: every row met** - stand 9,900 / 11,850 / 9,725 / 11,550 ms, 36/36 coverage, zero mismatches, p99 12.9 / 11.8 ms. **Level 1:** rows 1, 2, 4, 5 met (stand median 14,212.5, tail 14,750); row 3 at class 100 (the faster library trip waits 67.1 against 48.1 percent) ruled met in substance by the owner. **Both gates passed.**

**Own flag adopted, libraries re-explored (2026-09-24, [record](../../movement/records/2026-09-24-own-flag.md)):** level-2 stand 9,900 / 11,850 / 9,725 / 11,550 ms (gate row 2 met), level-1 stand 11,650 / 14,750 / 14,475 / 13,950 ms; floors on three maps 36/36. Next: both gates re-taken on build `ownflag-build-20260924-c`.

**Own flag at the stand (2026-09-24):** the forecast refused the bot's own flag at its stand, so at Speed the first edge of the upper route was walked past and every replan went to the lower floor. `bot_forecastOwnFlag` (test) gives the level-2 stand **9,900 / 11,850 / 9,725 / 11,550 ms**; Kejim and Dantooine floors 36/36, ctf4 35/36 (a drop-edge stall beside a mover), the level-1 library stops applying. Owner ruling pending.

**Level-2 library (2026-09-24, [record](../../movement/records/2026-09-24-level2-library.md)):** stand 13,075 / 13,425 / 13,525 / 13,450 ms at classes 75-150, deterministic; floor 36/36. The gate's 13,000 ms is not met at any class. Next: explorer route branching.

**Level-2 gate declared (owner, 2026-09-24):** [below](#level-2-exit-gate-2026-09-24). Every class of the level-2 stand at or under 13,000 ms (reference 9,000); pre-gate floors on Kejim, Dantooine v3 and ctf4. Baseline 13,575 / 15,600 / 15,200 / 13,575 ms ([record](../../movement/records/2026-09-24-level2-baseline.md)): the first hop under Speed comes 3.5 s after release, and classes 100/125 take a hop into a pillar.

Latest completed experiment: [the Force-pool divergence](../../movement/records/2026-09-24-force-pool-divergence.md), 2026-09-24. Closed: at level 1 the search does not read Force. Pinned dash2 worlds at Force 0 and 100 give the same times, and at a shared start level all 160 decision lines match; the 2026-09-23 difference was a 1,700 against 1,750 ms start.

Before that, analysis: [map symmetry and the graph grid](../../movement/records/2026-09-24-map-symmetry.md), 2026-09-24. The grid effect on Kejim is not directional (it favours red at respawn4 and blue at respawn5); a 16-unit grid would not align Kejim, a 12-unit offset would. `ctf_dantooine_v3`, the map in play, is itself asymmetric near both flags, so the owner chose no global refinement: a Kejim grid-offset diagnostic arm and local refinement near drops are open below level 2.

Before that: [red against blue, respawn4](../../movement/records/2026-09-24-respawn4-mirror.md), 2026-09-24. The 32-unit graph grid falls 8 units differently on Kejim's two sides, so blue's route ran along the chasm lip and braked; steering points are now pushed 16 units off drops by default (`bot_moveDropPush`), no death over 324 pinned floor worlds, -12.1 s net.

Before that: [level 2 against level 1](../../movement/records/2026-09-24-level2-vs-level1.md), 2026-09-24. On build `d`, level 2's median beats level 1 on 12 of 12 Kejim cases over three repetitions (0.5 to 7.0 s), and dash2 takes 19.6-20.8 s against 29.0-29.3; the 2026-09-23 "slower on 10 of 12" item is closed. Level 3 is set aside until level 2 is good (owner).

Before that: [the level-1 gate](../../movement/records/2026-09-24-level1-gate.md), 2026-09-24. **The gate is passed** (owner ruling: row 3 met in substance). Build `gate-build-20260924-d`, production configuration: floor 36/36 with zero parity mismatches and identical decisions on six cells; stand 13,625 / 13,975 / 12,300 / 14,075 ms (median 13,800, tail 14,075); 72/72; one-bot p99 11.4 / 10.4 ms. Row 3 is above its control at classes 100 (a walk-in to the flag) and 150 (equal waiting frames, fewer grounded ones). Two repairs made it: the flag pickup rule in `ArrivesAt` and the harness disarm on a taken pre-search. Next: level 2.

Before that: [lifts](../../movement/records/2026-09-24-lifts.md), 2026-09-24. A resting binary mover is floor in the graph, walk and forecast until it can next move. Lift edges ride up (board on the platform, wait for the call) and down (stand in the top call, board the arrived lift, ride it back), run by one ride state in the executor; the forecast refuses a lethal fall and honours a call's re-arm. ctf4 three arms 108/108 (control 103/108), climbs-off floor 36/36 on 36 rides up, Dantooine and Kejim 36/36 with Kejim's identity unchanged; times unchanged. Next: the level-1 gate.

Before that: [wall climbs](../../movement/records/2026-09-24-wall-moves.md), 2026-09-24, after [ctf4 and Dantooine](../../movement/records/2026-09-23-ctf4-dantooine.md). Bots send 8 ms commands only while climbing (owner decision; 25 ms cannot sustain a run up a wall); climb edges are discovered on built graphs and validated by rollout. ctf4 0/36 to 71/72 over two floors, Dantooine 36/36 with climbs in 34 cases, Kejim floor and library stand times identical. Next: lifts.

Before that: [heading sweep and the build-k library](../../movement/records/2026-09-23-heading-sweep-k.md), 2026-09-23. The heading sweep is safe on build `k` (four floors 36/36) but not faster and stays opt-in. The decision explorer re-run on build `k` (successor aim off, the approved settings) gives a library the bot selects by itself: level-1 stand **13,625 / 14,125 / 13,400 / 14,075 ms** at classes 75-150, every entry hit, two repetitions identical, median 13,850 against 16,437.5 without it; two library floors 36/36.

Before that: [flight mode 2](../../movement/records/2026-09-23-flight-mode-2.md), 2026-09-23. One strafe side a hop, now the default: key changes in flight fall from 20 to under 1 a second, the stand run's four-class mean from 19,744 to 17,775 ms, two floor pairs 36/36 with level 1 1.4 s a case faster. The seed library's tapes predate it and need re-exploring. Same day, build `flight2-build-20260923-k`: run-ups and the walk keep speed (longer momentum chord), hop lines pinned to the search's aim points, wall clearance in both planners, a route check and an 8-unit arrival margin; dash2 28.6 s (from 33.75), Kejim stand 15.5-16.9 s, two floors 36/36. Open: Kejim sequence joins where every tap-now hop crosses the chasm.

Before that: [goal-chain uptime, landing jump, lip guard](../../movement/records/2026-09-23-goal-chain-uptime.md), 2026-09-23.

Before that: [search speed](../../movement/records/2026-09-22-search-speed.md), 2026-09-22. Rollouts cost half what they did (23-24 to 11.6-13.6 us a simulated step), with decisions identical on the six cells and the floor 36/36 on build `row5-build-20260922-n`.
- **The changes:**
  - the steering search rewritten, bitwise identical on 2,000,000 checked cases;
  - the world copy and whole-world guard once per scope;
  - lone rollouts run in scopes of their own, instead of hashing the world byte by byte;
  - a corridor memo and a trace cache.

  All are unconditional since the owner promoted them.
- **One bot:** p99 22.3-22.8 ms at budget 0, and 11.4-11.7 ms at budget 4 (2 frames over 25 ms per 800).
- **Profiling.** It showed that collision traces are only 17-19 percent of a rollout, so a game-side trace is worth more for threads than for speed.
- **Human runs.** The owner's three runs spend 0.25 to 2.7 s walking after the start. The best two land touch-and-go (one command); every walk bleeds 200+ units/s. The bot is grounded 1.5 to 2.7 s a trip (`.codex-research/human-walk-20260923-a/`).

Current next goal (2026-09-22 owner direction, eleventh revision): **strafe quality with one bot first; multi-bot cost later.** Multi-threaded search is a backup.

Where it stands ([row 5 record](../../movement/records/2026-09-22-row5-planner.md), section 5):
- **One-bot row 5.** Build `row5-build-20260922-f` with `bot_moveSearchBudget 4`, the deadline-sized chunks, and decisions identical on the six cells. The floor is 36/36. p99 is **12.5 to 13.9 ms** against the 25 ms frame; with the budget off it is 36 to 40 ms.
- **What still overruns.** Three frames per 800 exceed the frame: the first search at spawn, the search after the fixture's declared shove, and at most one chunk.
- **Owner decisions, 2026-09-22 (ruled):**
  - Row 5's criterion is p99 inside the frame. It is **met** at budget 4 (p99 11.0-13.9 ms across builds f to o).
  - `bot_moveSearchBudget` 4 is the engine default and the production configuration (build `row5-build-20260922-p`); 0 turns it off.
- **A safety finding for strafe quality.** An unsettled spawn pose in the cost fixture (about one world in five) makes the bot fall to death at 7.25 s, with the budget on or off.

- **Search speed** ([record](../../movement/records/2026-09-22-search-speed.md)): a default-off, exact trace cache hits 54 to 72 percent of traces but saves only about 10 percent. Forecast profiling shows where rollout time goes: the walk's per-step corridor checks (about 40 percent), the per-rollout whole-world copy and compare (about 33 percent), Pmove (21 to 25 percent). Exact fixes for the first two are next.

**Next session (owner, 2026-09-22): the state planner in the reroute pass.** It costs as much as the rollouts in the worst remaining frames: 7-14 calls at about 5 ms on the seed. Exact speedups first: per-cell neighbour edge lists in scan order, and a memo across searches keyed by graph generation.

The state planner step is done ([record](../../movement/records/2026-09-22-planstate-lists.md)): 20-33 percent cheaper per expansion, decision-identical, with no measurable p99 change. **Next (owner, 2026-09-22): the rollouts**, which are 58-71 percent of the frames over 25 ms. First step done ([rollout replay](../../movement/records/2026-09-22-rollout-replay.md)): exact, rollout time -7 percent, default off pending the owner. Then report the gate, fix the spawn-fall defect, and start level 2.

- **Owner decisions, 2026-09-22:**
  - row 5 is one bot; strafe quality comes before multi-bot cost; threads are a backup;
  - the research seed is production;
  - respawn5 `jumppad` frames are accepted under row 1;
  - the `link` rung is removed.
- No frame-local candidate or selection work at level 1; level 2 waits for the gate.

The [trigger shadow measurement](../../movement/records/2026-09-21-trigger-shadow.md) and
[rescue follow-up](../../movement/records/2026-09-21-trigger-shadow-rescue.md) are corrected after
review: `swept` tests an enclosing axis-aligned box against brushes, not the actual
swept hull. Its 107/60 `swept_not_endpoint` cases remain unresolved; they are not
proven protective crossings. The 39 straight-search blue-flag verdicts are initial
`arrives` conversions, not final admissions or coverage of all 51 census
disagreements. Later chain/path gates and other rollout paths remain unmeasured.
Keep trigger admission conservative and the default-off diagnostic off for speed
runs. Deprioritize an admission repair because no speed payoff is demonstrated;
this is a priority decision, not proof that only 30 refusals are recoverable.
The diagnostic changes throughput, so its absolute counts describe instrumented
searches and stable observed rates are not guaranteed for uninstrumented runs.

Phases are ordered so each one is useful on its own and testable in the
movement benchmark before the next begins. Dates are recorded when a phase
lands; none are promised here.

## Level-2 exit gate, 2026-09-24

Owner decision, 2026-09-24. Level 2 is Force 50 with the Speed purchase
(the Kejim L2 cases' declared spend). The owner gave the speed row as
"roughly 9 to 13 seconds"; recorded here as a 13,000 ms bound on every class
with 9,000 ms as the reference, not a gate (the level-1 gate's 15-second
figure played the same part). The gate is formally Kejim only, but it is not
taken until level-2 floors on Kejim, Dantooine and ctf4 pass first.

| Row | Requirement | Status |
| --- | --- | --- |
| 0. Pre-gate floors | Level-2 floors on Kejim, Dantooine and ctf4 in the production configuration: every case completes, zero deaths, no forbidden spend. Dantooine is the map in play, `ctf_dantooine_v3` (`tools/fixtures/movement/dantooine_v3/cases.json`) | **Met once, 2026-09-24** (build `droppush-build-20260924-b`, `.codex-research/l2-prefloor-20260924-a/`): Kejim 12/12 (7,975-13,575 ms), Dantooine v3 12/12 (12,000-17,575), ctf4 12/12 (11,950-18,875); zero deaths, owner `move` only. Re-take on the gate build |
| 1. Safety | As level-1 row 1, on the level-2 cases: Kejim floor 36/36, zero deaths, movement owner, no forbidden spend; a harness-on diagnostic arm with non-zero comparisons and zero mismatches, and identity with the harness-off arm | Open |
| 2. Speed | `red_out_L2_stand` pinned at classes 75, 100, 125 and 150 (`--corner-class`), at least three repetitions a class: **every class at or under 13,000 ms**, tail included. Reference 9,000 ms | Open. Baseline, build `droppush-build-20260924-b`, three repetitions, deterministic: **13,575 / 15,600 / 15,200 / 13,575 ms** (`.codex-research/l2-baseline-20260924-a/`). **Own flag on by default, with re-explored libraries ([record](../../movement/records/2026-09-24-own-flag.md)): 9,900 / 11,850 / 9,725 / 11,550 ms, two worlds a class, identical - met at every class; the gate re-take (rows 1, 3, 4) is next.** Earlier the same day, **level-2 library** ([record](../../movement/records/2026-09-24-level2-library.md)): **13,075 / 13,425 / 13,525 / 13,450 ms**, two worlds a class, identical; 75-525 ms over. Level 1 with the library 13,625 / 13,975 / 12,300 / 14,075 |
| 3. Coverage | All 12 Kejim level-2 cases timed, median and tail over at least three repetitions | Open. Unpinned 2026-09-24: 8,700 to 13,575 medians ([record](../../movement/records/2026-09-24-level2-vs-level1.md)) |
| 4. Cost | One bot, p99 inside the 25 ms frame at budget 4, with the Speed purchase in the search | Open |

## Level-1 exit gate, 2026-09-21

Owner decision after the 2026-09-21 progress review. The phase-3 step-2
gate at level 1 ("no slower than the greedy chains") was met on 2026-09-16
and was too weak: the level-1 stand run then spent five days under an
open-ended speed target with no stopping rule. This gate replaces both. It
is the definition of "level 1 works", and level 2 is not attempted until
every row passes on one build, reported together.

The 15-second figure is retained as the expert reference (the owner's own
14.8-second run on the same line), reported beside every result, and is
not a gate. The project goal is an intermediate-human bot; the [leg
budget](../../movement/records/2026-09-20-leg-budget.md) shows the bot already reaches
583 units/sec in flight and that 15.0 s over the fixed 7,161-unit leg needs
a 477 units/sec mean, so the target is not excluded by the physics and stays
open as a stretch reference.

| Row | Requirement | Status |
| --- | --- | --- |
| 1. Safety | 36 of 36 in the production configuration (production cache, library on, parity harness off), zero deaths, movement owner on every frame, no forbidden spend; plus, on the same build, a harness-on diagnostic arm of the four declared stand cells and both seeded regressions with a non-zero parity comparison count per world and zero mismatches, and the harness-off production arm shown to make identical decisions on those cells. Re-worded 2026-09-22: the harness is a qualification diagnostic, not a production setting, so the count is required where it is armed and identity is required where it is not | Open on the production build: floor 36/36 with zero deaths and no forbidden spend (production-graph record); the harness-on arm and the identity check are to be taken with the final rows. **Seed, 2026-09-22 ([record](../../movement/records/2026-09-22-seed-gate.md)): met but for ownership** - floor 36/36, seeded regressions 2/2, respawn4 at class 50 pass, zero deaths, harness-on counts 667 to 894 with zero mismatches and decisions identical on six cells; the respawn5 cases report `jumppad` beside `move`, for the owner to rule on |
| 2. Speed as a distribution | `red_out_L1_stand` median at or under **16,500 ms** and tail (worst completed repetition) at or under **18,000 ms**, over the four declared start classes (75, 100, 125, 150 via `--corner-class`) weighted equally, at least six matched repetitions per class. 16,450 ms has been reproduced three times; the row asks for that outcome made reliable, not a new discovery | Open. Latest: declared-weight mean 17,394 ms, eight-pair median 18,125 ms. **Seed, 2026-09-22: met** - median 15,962.5 ms, tail 16,725, six repetitions per class, production configuration |
| 3. Mechanism | Waiting share (`precommit_grounded` plus `uncommitted_grounded` over the leg's grounded frames, `tools/movement_leg_budget.py` under LEG_BUDGET2) reported per class on the same worlds as row 2, library and control. Requirement, re-based 2026-09-22 a second time: **at no class may the library world wait a larger share than its control**, and rows 2 and 3 must move together class by class. No fixed threshold: the 15 percent first declared predated any fast trip, the 35 percent that replaced it is beaten by the class-100 explorer trip itself (16,725 ms, 58 percent waiting), and in 24 worlds no candidate was admitted at any waiting frame, so the share at those sites is the vocabulary's. The row's job is attribution: a row-2 gain with row 3 unchanged is a selection artifact and is not taken | Open. Class 75 30.4 against 71.2, class 125 37.9 against 37.9, classes 100 and 150 to be re-measured with reproducing tapes. **Seed, 2026-09-22: met** - 30.4/55.1/37.9/35.0 against 71.2/59.5/37.9/50.5 |
| 4. Coverage | Every level-1 case timed, not only green: median and tail per case over at least three repetitions, so the respawn cases have a number. Added 2026-09-22: with the library on, at least one start whose tape falls back partway (a displaced or respawn start that selects on a non-exact rung) timed against its library-off control, so the report shows what the library does off the explored cells, not only that they complete | Open. Only the stand case has repeated timings. **Seed, 2026-09-22: met** - 72 of 72; the library selects off its cells only on the red-return legs (`link`), neutral to harmful there |
| 5. Cost | Search enabled without the parity harness, a declared per-frame CPU budget met with at least four bots on one server, results invalidated on root/goal/world change, no idle wait induced by the controller | Open. This is the runtime-qualification milestone row, pulled into the gate. **Seed, 2026-09-22: not met** - p99 98 to 100 ms, worst 216 to 219 ms, four bots (v3: 60 to 65, 106 to 108) Planner and finish phase made cheaper and resumable, decision-identical; tail unmoved, 37 to 42 frames over 25 ms ([row 5 record](../../movement/records/2026-09-22-row5-planner.md)) **Re-scoped 2026-09-22 (owner): one bot.** The four-bot requirement is deferred until strafe quality is established; multi-threading is the recorded backup **One bot, budget 4 with the deadline (build f): p99 12.5 to 13.9 ms, three frames over 25 ms per 800. **Criterion ruled 2026-09-22 (owner): p99 inside the frame, with budget 4 the default: met** (build o: p99 11.0-12.8 ms) |

Rows 2 and 3 are read from the same worlds so a speed change is always
attributed to a mechanism change. A build that improves row 2 while row 3
worsens is not accepted; that is how a one-class selection tweak looks. The
old requirement that a change fix a named benchmark case still applies to
safety; the gate rows are the acceptance for speed.

### Plan review, 2026-09-22: decisions

Owner decisions after a review of this plan. Evidence:
[plan review checks](../../movement/records/2026-09-22-plan-review-checks.md).

- **What passing level 1 means.** Rows 2 and 3 are met by prepared plans,
  which are decision tapes the bot selects itself. They are not met by
  independent search: under the ranking alone the stand trip on v3 is 16,375
  to 20,650 ms. A pass therefore means "fast on the explored stand starts and
  safe everywhere". Row 4 now also times a start where the library falls
  back partway, so what the library does off its cells is reported.
- **Row 3 keeps its relative form.** It is still the attribution check. A
  relative comparison is close to automatic when the library replays the
  explorer's own trip, so row 3 is not by itself evidence of a better
  mechanism.
- **Row 5 must not invalidate the tapes.** Row-5 changes that land after
  tapes are explored must give identical decisions on the reference cells,
  as the search budget did. That covers memoising `RerouteFrom` and spreading
  the finish phase. A change that alters decisions lands before exploring,
  never after.
- **Check feasibility before a chain.** Before a multi-hour explorer chain,
  one cheap check must show the graph can carry near-bound trips. For v3
  that check is done, and it does not: the seeded decisions give a 16,975
  median.
- **Split the chains.** Explorer chains run the four declared classes first
  and are read before classes 0, 25, 50 and 175. Row 2 is measured on the
  four declared classes alone.
- **Directives must be carried, or route roots are measurements only.** A
  route root's result is a tape plus a planner directive. Either the library
  carries directives before the gate campaign, or route-root results stay
  explorer measurements and move no row.
- **The start-phase hold is refuted and removed.** Holding to phase 125
  gives about 19.2 to 19.35 s from every class.
- **The production graph is the seed** (owner, 2026-09-22). It was set aside only for the respawn4 deaths, and those are repaired. The v3 explorer chain is not run. Rows 1 to 4 met on it, row 5 not ([record](../../movement/records/2026-09-22-seed-gate.md)).
- **Respawn5 `jumppad` frames are accepted under row 1, and the library's `link` rung is removed** (owner, 2026-09-22). The upstream jump-pad controller owning a pad flight is not the legacy owner. The link rung was neutral or slower wherever it answered.

### The mechanism, and why the next work is prepared plans

At the waiting frames the search proposes and refuses: 1,029 candidates
judged, none admitted, 731 refused for a trigger contact the forecast cannot
model, 154 fallen, 110 out of corridor
([leg budget](../../movement/records/2026-09-20-leg-budget.md)). The live search's
vocabulary is one or two hops from a 24-unit run-up, and its validator is a
static-world forecast. Neither is a candidate-count problem, so more
candidate-fan or selection screens are not expected to move row 3.

The owner's direction is to promote the prepared-plan integration milestone
from "later" into the level-1 mechanism: a library of plans found offline,
validated in the game rather than the forecast, indexed by entry envelope
(cell reach, speed band, heading cone, landing-frame tap) so that a live
landing usually falls inside one. This is the refined method's "offline
explores broadly, online works from a small budget" applied to row 3.

One prior attempt is on record and must not be repeated as-is: the 94,711-edge
exploration seed of 2026-09-16 made level 1 slower than the greedy chains
with two legacy fall-backs ([search review](../../movement/records/2026-09-16-search-review.md)),
because a learned edge is admitted only from the cell it was learned at, inside
`GRAPH_STATE_BAND` 48 units/sec, `GRAPH_STATE_CONE` 30 degrees and
`GRAPH_STATE_REACH` 48 units of its takeoff, and the landing that reaches a
cell rarely repeats that state. A larger library of the same shape is not the
answer; the diagnostic below decides what shape is.

### The plan-coverage diagnostic

Read-only analysis of retained evidence. No native run, no source change,
no rebuild. It answers one question: **at the frames where the bot waited,
would a plan in the library it already has have applied?**

- Inputs, bound by hash in the report: the sixteen confirmation worlds of
  `.codex-research/successor-aim-20260920-a/` (`confirm-0-r1..r8`,
  `confirm-1-r1..r8`), and the merged graph cache the 2026-09-16 seeded arm
  ran with (`.codex-research/move-review-20260916-seeded-b/`), plus the
  current campaign's own learned edges for a second column.
- For every grounded run classified `waiting_for_a_committable_plan` by
  `tools/movement_leg_budget.py`, take the live state at the first waiting
  frame: position, horizontal speed, heading, landing-frame tap bit, cell.
- Test that state against every strafe edge in each cache with the exact
  envelope rule the game uses (`Graph_StrafeAdmits` in `bot2_graph.c`: cell reach,
  speed band, heading cone, tap bit). Record per waiting run: the number of
  edges whose envelope admits the state; if zero, the nearest edge by each
  axis separately, so the report says which axis excluded it.
- Report, per cache: waiting runs covered (at least one admitting edge),
  uncovered with a near miss on exactly one axis (name the axis and the
  margin distribution), and uncovered with no edge within twice the
  envelope on any axis. Also the same three counts for the committed runs
  as a control, so a low covered figure is not just what every frame looks
  like.
- Do not extend the tool into the game. A new script beside
  `movement_leg_budget.py`, with a unit test on a hand-built cache and a
  hand-built trace, following `test_movement_leg_budget.py`.

The result picks the explorer, and the plan does not choose before it:

| Finding | Meaning | Next work |
| --- | --- | --- |
| Most waiting runs covered | The library applies and the live admission or forecast refuses it | Native re-validation of library plans and a trigger-aware admission; the trigger shadow work returns with a demonstrated payoff |
| Most uncovered by one axis with small margins | The envelope is the limit, not the states | Widen or re-band the envelope and re-validate the widened edges natively; measure rows 2 and 3 |
| Most uncovered with no edge near on any axis | The states are unvisited | Sample plans from real landings in retained worlds instead of synthetic corridor states; density at the observed waiting states, not volume |

Whichever branch is taken is one work order with its own paired control,
screened against rows 2 and 3 together, with rows 1, 4 and 5 reported on the
confirming build.

**Reported 2026-09-21**: [the plan-coverage record](../../movement/records/2026-09-21-plan-coverage.md).
34 of 60 waiting runs covered against 66 of 107 committed runs, none
uncovered with no edge near, 26 uncovered on exactly one axis (cone 18, reach
4, speed 4) and the cone margins mostly above 110 degrees. The heading is not
retained at frame resolution and is reconstructed, with its error measured at
a 7.7-degree median and 18.9-degree worst; the tool's envelope agrees with
the game's own logged `envelope=` verdict on 130 of 150 records, 18 of the 20
disagreements resolving once the unlogged cell choice is relaxed. The
campaign's own caches are the seeded library to within four edges, so the
second column is not independent evidence. That picks the first branch, with
the qualification that the committed control is covered no less often, so
coverage alone is not a measured speed opportunity.

### Next work order, 2026-09-21: the library-rescue arm

> **Arm A ran on 2026-09-21 and is not accepted**
> ([record](../../movement/records/2026-09-21-library-rescue.md)). 1,571 edges offered, 113
> admitted, 45 committed over 24 worlds, so learned edges do transfer; the
> declared-weight mean is 19,416.4 ms against the control's 17,427.7 and the
> waiting share falls only 60.3 to 58.3 percent. Both rows improve at classes
> 75 and 100 and both worsen at 125 and 150, which carry 77 of the 93
> declared weight. 44 of 48 worlds complete, zero deaths, movement owner
> only, 30,406 parity comparisons with zero mismatches and refusals.
> `bot_moveLibraryRescue` stays default off pending the owner's disposition.
> **Arm B does not run**: the leading rescue refusal is `corridor`, 726 of
> 1,458, not objective-flag contact. The order's failure branch - whole
> chains validated as chains - is the proposal on the table, and no explorer
> is built before the owner picks one.

Owner decision after the [plan-coverage diagnostic](../../movement/records/2026-09-21-plan-coverage.md).
The decision table's first branch names "native re-validation of library plans
and a trigger-aware admission". Two retained measurements bound what each half
of that can be worth, and they change which half goes first.

**The trigger half is already priced, and it is small.** The
[trigger shadow measurement](../../movement/records/2026-09-21-trigger-shadow.md) found that
of 1,960 trigger refusals, **1,750 (89.3 percent) are contacts the game itself
would dispatch**; `trigger_hurt`, the pit, is 55 to 60 percent of all contacts
and [classified genuine](../../movement/records/2026-09-20-trigger-classification.md). The
false-refusal subset is 210: 51 are the objective flag, which `StraightArrives`
already rescues at arrival; 48 are box-not-brush on the pit, whose repair
needs `trap->EntityContact` inside the search and an unmeasured cost; 107 are
unresolved enclosing-box contacts. Re-validating refused candidates natively
would therefore hand back at most a tenth of them, and the
[rescue follow-up](../../movement/records/2026-09-21-trigger-shadow-rescue.md) shows the
recoverable ones cluster at the objective, not at the mid-leg waiting frames
where row 3 is lost. The candidates the search proposes at those frames
genuinely fly into the pit.

**The library half has never been tried.** `Graph_StrafeAdmits` is consulted
in exactly two places in `bot2_move.c`: on the planned path's own edge at the
bot's index, and at a chain's second-hop entry. At a landing where the straight
search admits nothing, the strafe edges whose envelope admits the live state
(a median of 20 at a covered waiting frame) are never rolled out. Nothing
refuses them; nothing offers them. That is the mechanism the coverage result
points at, and it is the one this arm tests.

#### Arm A, first: library rescue at the waiting frame

- Trigger: the straight search at a grounded frame returns with no admitted
  candidate. Ordinary walking, the near-goal exclusion and an invalid path
  are excluded exactly as the short-landing proposals are.
- Proposal: every strafe edge in the loaded graph whose envelope admits the
  live state at the bot's cell, by the unchanged `Graph_StrafeAdmits`,
  excluding edges already on the avoid list. Cap at eight, ordered by the
  edge's learned cost; the cap is a budget, not a ranking claim.
- Validation: each proposal is rolled out from the live root by the same
  `RunCandidate` the straight search uses, aimed at the edge's learned
  landing with its learned tap, and judged by the one judge beside nothing
  (the search had admitted nothing). Every gate stays: pit, trigger contact,
  corridor, fall, path progress, chain entry. No admission threshold moves.
- Switch: one default-off cvar, `bot_moveLibraryRescue`, added as an
  experiment under principle 7. After the screen it is either promoted with
  the default on and the cvar removed, or deleted with its code. It does not
  join the parked research switches.
- Telemetry: each rescue search prints, after it returns and never inside
  it, the number of admitted edges offered, the outcome of each rollout by
  the existing outcome names, and whether one was committed. The refusal
  census of rescue candidates is the second result of the arm, whatever the
  timing says.

Prediction stated before collection: if learned edges transfer across the
envelope's width, row 3 falls because a library flight is committed where the
walk resumed. If rescue candidates are refused at the search's own rate for
the same reasons, learned edges do not transfer from near-envelope states,
and the next explorer is whole chains validated as chains; the fifteen
landings facing more than 110 degrees away from every exit already point
there.

#### Arm B, only if A leaves row 3 high: the item-branch repair

The one trigger repair the shadow record proposes: make the forecast's
`ET_ITEM` branch exclusive, as `G_TouchTriggers`'s if/else is, so an item
that misses its own pickup bounds is not refused on a box native never
consults. Carry a rollout identifier in the shadow record so the 51
objective-flag refusals can be joined to their `[SPIKE]` outcome and the
already-rescued ones counted rather than guessed. Not box-not-brush, not the
swept box; those keep conservative admission. Arm B runs only if arm A's
rescue census shows objective-flag contact as the leading refusal at the
waiting frames.

#### Screen, for either arm

- Case `red_out_L1_stand`, the four declared start classes by
  `--corner-class`, six matched pairs per class, arms alternated in sequence,
  never in parallel batches. The control is the same build with the switch
  off. Standard settings: `bot_moveSuccessorAim=1 bot_moveReroute=1
  bot_moveExitSpeed=500 bot_moveStatePrefer=1 bot_moveSuccessorFan=0
  bot_forecastItems=0 bot_forecastTriggerShadow=0`, playback and heading
  sweep off, the seeded graph.
- Rows 2 and 3 from the same worlds: completion median and tail at declared
  and equal weight; waiting share by `tools/movement_leg_budget.py` under
  LEG_BUDGET2; the candidate-outcome census at waiting frames beside it.
- Accepted only if row 3 falls and row 2 does not worsen at declared weight.
  A row-2 gain with row 3 unchanged is a selection artifact and is not taken.
- A passing screen then takes the 36-case floor, both seeded regressions,
  parity with a non-zero comparison count and zero mismatches, and in-trip
  rollout cost against the control, before anything is called a mechanism
  win. Row 4 and row 5 are reported on that build, not implied.
- Fresh unique output directories, sources frozen during collection,
  binding by hash, failed worlds retained. Nothing is installed.

### Next work order, 2026-09-22: the decision explorer

Owner decision after the [decision-tape record](../../movement/records/2026-09-22-decision-tape.md).

**What the transfer result established.**

- The start class is a physics phase, not a decision-phase artifact: the
  class modulus of 200 ms is the least common multiple of the 25 ms server
  frame and the 8 ms fixed Pmove step. Within a class the trip is
  deterministic to one frame at every absolute level measured; across
  classes the same first decision lands in a different state and the second
  or third decision is refused (class 75) or not offered (classes 100 and
  150). No single decision sequence is available whole from every start.
- The ranking's early choices are not the best available: forcing only the
  reachable class-125 prefix is 450 ms faster at class 100 and 550 at 150.
- The decision tape is a closed-loop prepared-plan executor in diagnostic
  form: it names what to choose, the live search still proposes, rolls out
  and judges from the actual state, and a refused or absent entry falls back
  to the ranking. That is the prepared-plan lifecycle the architecture asks
  for, with one owner and no new controller.

**The explorer.** Per start class, a beam search over decision sequences
executed by the live fixture, because within a class the fixture is the
exact simulator and nothing has to be forecast:

1. Depth 0 is the class's control world. Its commit line at each decision
   and the `[SPIKE]` lines of that search list the admitted candidates.
2. To branch at decision k with a prefix of k decisions: run one world with
   the prefix as a decision tape and `bot_moveSelectNth` at the commit after
   the prefix, `bot_moveSelectCand` naming one admitted candidate of that
   search. Every world runs to the objective with the ranking choosing after
   the branch, so every world yields a complete trip time and a full decision
   sequence, and a prefix that stops hitting is detected from its census.
3. Keep the best B sequences by completed time at each depth (B of 3 to
   start), branch each over every admitted candidate at its next decision,
   and stop when the prefix covers the whole leg or no branch improves on
   the incumbent for two depths. Record every world.
4. The result per class is its fastest complete trip as an 11-or-so-entry
   decision tape, with the census that proves every entry hit on a fresh
   world.

**Stage 1, before the search runs.** Three checks and one repair, each
recorded:

- Index forcing is reproducible within a class: at class 75, force two
  different admitted candidates at the second commit under the class-125
  prefix, two worlds each; each pair must be identical to the frame.
- Wall-clock per world at the bench's default timescale, so the beam is
  sized from a number; the campaign runs detached with its progress file,
  never inside a foreground shell.
- The eight phases: the harness has only ever landed classes 75, 100, 125
  and 150. Find whether the class window can reach the other four (0, 25,
  50, 175) and report which classes the explorer can cover.
- `strafe_fixture_build.py` exits 0 on a failed DLL build; make it fail,
  with a regression.

**Stage 2, the search**, first at class 75, the slowest, then the other
reachable classes, each a detached campaign with retained worlds and a
per-class record.

**Stage 2 at class 75, 2026-09-22.** [Record](../../movement/records/2026-09-22-decision-explorer-c75.md). The beam reached **15,950 ms** at depth 3 against the control's 21,125, stopped after two depths without improvement (163 completed worlds), and the tape reproduces on three fresh worlds released up to 600 ms apart, every entry a hit, decision for decision. The judge's own rank barely predicts the completed trip at the three decisive frames (Spearman 0.18, 0.04, 0.34; the eventual winner ranked 7th, 6th and 5th of its set), which is why the explorer is the mechanism and a ranking repair is not. The landing-cell identity is coarser than the outcome: candidates it calls one decision span up to 1,625 ms at one frame, so the tie rule matters and the identity may need an exit-state term before tapes are asked to transfer across phases.

**Stage 2 at the other three declared classes, 2026-09-22.** Campaigns under `.codex-research/decision-explorer-c{100,150,125}-20260922-a/` on the stage 3 build, same beam and stop rule: class 100 **16,725 ms** at depth 3 against its control's 19,375 (150 completed worlds); class 150 **15,975 ms** at depth 2 against 17,575 (140 worlds); class 125 found nothing better than the ranking's own **15,925 ms** in 44 worlds, so its tape is the retained self-control leg. With class 75's 15,950, the four explorer bests have an equal-weight median of 15,963 ms and a worst of 16,725, inside row 2's bounds if the library selects and reproduces them, which is the next measurement. Every class converged within three decisions, and at every class the ranking's own trip was reachable by changing only the first two or three choices.

**Stage 3 landed, 2026-09-22.** [Record](../../movement/records/2026-09-22-tape-library.md). Both conditions hold on build `tape-library-build-20260922-b`: with `bot_moveTapeLibrary=1` and no tape named by the bench, the bot forms its key (leg `t1e83` from its own team and objective entity, start cell 0, phase `level.time % 200`), selects on the exact rung and reproduces 15,950 ms at class 75 (three worlds) and 15,925 at class 125, every entry a hit; at classes 100 and 150 the ladder falls to the nearest phase, 1 to 2 entries hit, and the trip completes under the ranking at 19,150 and 17,025 ms against controls of 19,375 and 17,575. Inert when off. One design correction: the objective entity is not yet set on the release frame, so the key completes at the leg's first think. The parity zero-comparison count is a print condition in the tally, not lost coverage; its repair changes verdict semantics and waits for the next build. Per-class campaigns for 100, 150 and 125 run next on this build.

**Four-class library measured, 2026-09-22.** [Record](../../movement/records/2026-09-22-four-class-library.md). Twelve library worlds, twelve controls, every library world selecting on the exact rung. Per class, library against control: 75 **15,950** against 21,125; 100 **16,875** against 19,375; 125 15,925 against 15,925; 150 **18,675 against 17,575**. Row 2: equal-weight median **16,412.5 ms** (met, bound 16,500), tail **18,675** (not met, bound 18,000), three repetitions of the six asked. The tail is a defect, not a limit: the class-100 tape hits 4 of 11 entries on a fresh world and the class-150 tape 2 of 10, because the landing-cell identity leaves several admitted matches at most entries and the judge's rank that breaks the tie is not stable between the explorer's branch world and a fresh tape-only world; class 75 and 125 reproduce by luck of that tie. Repair in progress: tie-break by smallest printed-aim gap before rank, and the explorer confirms every incumbent by a fresh tape-only replay before its time counts. Row 3 on the same worlds: 30.4, 58.3, 37.9 and 62.5 percent (library) against 71.2, 59.5, 37.9 and 50.5 (control); no candidate admitted at any waiting frame in 24 worlds. The class-100 explorer trip at 16,875 ms waits 58 percent, so the 35 percent bound is not what a fast trip does either; row 3 is to be re-read once the tapes reproduce, and its threshold may have to go while its attribution role stays. Row 1: two of three class-75 controls again reported zero parity comparisons; the tally repair is in the same work.

**Tie-break and tally repaired, 2026-09-22.** [Record](../../movement/records/2026-09-22-tie-break-and-tally.md). Ties among landing-cell matches are broken by printed-aim gap before rank; every library world now hits every entry at gap 0.0000 on build `tie-break-build-20260922-a`, classes 100 and 150 at their campaign figures 16,725 and 15,975 ms. One parity tally per leg reports the real comparison count (667 to 905 on 18 worlds; the old zero was a print condition). The explorer confirms every incumbent by a fresh tape-only replay. Row 2 at three repetitions: median **15,962.5 ms**, tail **16,725**. Next: the gate rows measured together on this build.

**Gate rows 1 to 4 measured, 2026-09-22.** [Record](../../movement/records/2026-09-22-gate-l1.md), build `tie-break-build-20260922-a`, 191 worlds. Row 1 **met**: floor 36/36 library off and on, both seeded regressions, zero deaths, owner `move`, non-zero tally in every world. Row 2 **met** at six repetitions: equal-weight median **15,962.5 ms**, tail **16,725**, every library world on the exact rung with every entry hit. Row 3 **met**: waiting 30.4/55.1/37.9/35.0 percent against controls 71.2/59.5/37.9/50.5, rows 2 and 3 moving together at every class. Row 4 **met unseeded** (72 of 72, one case 200 ms slower with the library) and **not met seeded**: with the 2026-09-16 research seed loaded, two respawn cases die in both arms and pass without it. Row 5 not measured. **The gate is not reported passed**, for two reasons that are now the work: the rows were not all measured in one configuration, and the seed that row 2's tapes were found on fails row 4; a tape also selects and hits nothing on a graph other than the one it was learned on. The production configuration is therefore the unseeded graph, tapes bind to their graph, the search is decoupled from the parity harness for row 5, and the eight-class explorer runs again unseeded.

**Graph binding, decoupling and the first cost measurement, 2026-09-22.** [Record](../../movement/records/2026-09-22-graph-binding-and-decoupling.md). Tapes and the library index carry `Graph_Identity`, a digest of the loaded cells and edges; a mismatched tape is refused on the census line and the trip falls back at once. The straight search runs on its own safety gates with the parity harness off, decisions identical to the harness-on cells. Two findings set the remaining order. **Unseeded runs are not cells**: ten worlds at one release level gave three times, because edges learned while the fixture holds the bot before release change the planner's candidates; the production graph must be a fixed cache from the bot's own runs and the hold's learning must not reach the leg. **Row 5 is far off**: four bots, production configuration, no tuning, per-frame movement cost p50 67.5 us, p95 36.8 ms, p99 96.9 ms, worst 273 ms against the 25 ms frame; 114 of 800 frames over a declared 5 ms budget, 96 percent of it the landing-frame search, which today runs to completion on the frame that triggers it with no time budget. A budget changes decisions, so it lands before the eight-class explorer, not after.

**Production graph and determinism, 2026-09-22.** [Record](../../movement/records/2026-09-22-production-graph.md). Pre-release learning was the whole cause of unseeded spread: worlds with the same pre-release learned set ran identical decisions, and one learned edge on the first route changed decision 0. The corner release now resets the learned set to the loaded cache (`Graph_ResetLearned`, fixture-only by construction, with a regression). The production cache is 676 edges merged from 144 unseeded gate worlds, identity `g1-0e5af7aba9b5e908`. On it, library off and harness off: class 125 ten of ten at **19,050 ms**, class 75 six of six at **19,150**, one sequence each across release levels 1,600 ms apart. Floor 36 of 36, zero deaths, no forbidden spend, both seed-killed respawn cases pass. Every earlier tape is refused on this graph by the binding, as intended; the explorer re-runs on it after the search budget lands.

**Search budget, 2026-09-22: decision-identical, cost unmoved.** [Record](../../movement/records/2026-09-22-search-budget.md). `bot_moveSearchBudget` (default off; a per-bot per-frame rollout count, never a clock) pre-searches the next boundary from the forecast's predicted state across the flight's frames and takes the judged set only when the actual state, both executors, every candidate and a validating rollout of the winner agree. All four reference cells identical on and off (seeded 15,925 / 21,125, production 19,050 / 19,150, every decision the same), 328 of 492 searches served in flight, floor 36 of 36. But p95/p99 and the worst frames do not move and the boundary total rises 1.6 to 2.1 times, so the switch stays off. The tail is not the candidate rollouts: the budget-off control on the production cache already has p99 near 200 ms and a 2.4 s worst frame. Row 5's next step is attribution of the expensive frames from retained evidence, with the reroute pass's per-candidate `Graph_PlanState` over 14,249 cells the named suspect; in progress in an isolated worktree while the eight-class explorer chain runs on `search-budget-build-20260922-c` with the production cache.

**Production explorer, first classes, 2026-09-22.** On the production cache (676 edges) the explorer converges about 1,500 ms behind the research seed at every class measured so far: class 75 **17,650 ms** (control 19,150), class 150 **17,125** (20,150), class 100 at 17,900 and still searching. The seed's fast trips used state-plan edges the cache lacks. The enrichment that keeps the graph made of real runs: resolve every graph edge named on a `reason=sequence` commit line in a retained world against that world's own saved cache (an index is local to the graph the world loaded; seeded and unseeded worlds never mix), and fold those edge records into the production cache with `movement_graph_merge.py`'s identity. Across the seven retained seeded campaigns that is **472 distinct committed edges**, each flown under live validation. Cache v2 must then pass the floor (both seed-killed respawn cases included), reproduce one cell per class, and the explorer runs again on it; the eight-class chain on cache v1 completes first so its worlds also feed the merge.

**Production explorer on cache v1, four declared classes, 2026-09-22.** Class 75 **17,650 ms** (control 19,150), 150 **17,125** (20,150), 100 **17,900** (20,750), 125 **16,275** (19,050), each confirmed by a fresh tape-only replay; equal-weight median **17,387.5 ms** against the 16,500 bound, so cache v1 cannot carry row 2. The chain was cut off during class 0 by a session restart (partial worlds retained, one orphaned server stopped). Cache v2 by the enrichment step above is in progress, with the floor, the determinism cells and the eight-class chain on it to follow.

**Cache v2, 2026-09-22.** [Record](../../movement/records/2026-09-22-production-graph-v2.md). `tools/movement_graph_enrich.py` resolved 12,079 committed edge indices across 2,806 retained worlds against each world's own saved graph (index order and the release-time truncation checked in the engine), refusing none: 470 new edges from the seeded campaigns plus 2,194 learned in the v1 explorer worlds, folded on v1 into **3,312 edges**, identity `g1-9631cbeaef2e5ba8`, every input bound by hash. Floor 36 of 36 on v2, zero deaths, no forbidden spend, both seed-killed respawn cases pass. One time per class: 125 **19,275 ms** six of six, 75 **19,450** four of four, each 225 to 300 ms slower than on v1 because the ranking now takes v2 edges from its first decision. The eight-class explorer chain runs on v2; whether it offers faster chains is its question. Row 5 is unmeasured on v2 and the planning pass suspected of the cost tail now runs over five times the edges; its attribution proceeds read-only in parallel.

**Row 5 attributed, 2026-09-22.** [Record](../../movement/records/2026-09-22-boundary-cost-attribution.md), read-only over the retained cost worlds. The frames of 0.5 to 3.4 s are not search: four of six sit in the command dispatch where no search runs, and every one coincides with `Graph_Save` rewriting the whole cache (0.9 MB on v1, 1.27 MB on v2) on a learn, 43 to 57 times a world, one stall pinned between the two prints of a single two-edge learn. Ordinary saves cost about 12 ms each and 0.6 to 0.75 s a world. Below that, p95 to p99 is one bot's whole landing search on one frame, 71 percent rollouts at about 2 ms a candidate and 3.3 ms a reroute call; `Graph_PlanState` is 1 to 3 ms a call and never overflowed in 1,646 calls. The search budget did not move the tail because the finish phase (three walks and up to 17 reroute calls) and the commit's two cache writes stay on the boundary frame. Work order, after the v2 chain releases the sources: (1) defer the autosave to a snapshot written at shutdown, from `Graph_Autosave` and at map prepare, behind `bot_moveGraphSaveDefer`; decision-identical because nothing reads the file before the next load, and the saved cache must be byte-identical between arms; (2) memoise `RerouteFrom` within a search on landing cell, exit state, goal, avoid list and a graph-mutation stamp, and clear the planner's slot table by stamp; (3) then spread the finish phase under the existing budget with per-pre-search walk buffers. Regression: the four reference cells identical off and on, then paired cost runs alone on v1 and v2. The declared 5 ms budget is a reporting line; the hard bound is the 25 ms frame.

**Seed deaths and planner reach, 2026-09-22.** [Record](../../movement/records/2026-09-22-seed-deaths-and-planner-reach.md), read-only. (A) All eleven seeded deaths of the two respawn4 cases are one trip from start class 50, and no world without the seed has ever run those cases at class 50, so seed and class are confounded and the gate record's probe did not isolate the seed. Mechanism: the planner routes over a pit crossing, the walker declines at `floor_missing`, the bot hands off to the **legacy owner**, whose stuck recovery jumps 1,776 units into the pit (`MOD_FALLING`). v2 holds one of the four crossings and six near the site; class 50 is unmeasured on v2. **Two defects, both level 1:** a plan is not replanned around a declined crossing, and a hand-off to the legacy owner still exists in the production path. (B) State candidates are only the single planned route's next strafe edge, so the explorer cannot force an edge off the route. v2 lacks four edges the seeded fast routes needed (51453, 48185, 41945, 59480), never committed because the fast trips flew straight flights along them; with 59480 restored, v2's own planner takes the seeded route unforced (12,131 ms against 12,124). Work order once the v2 chain releases the sources, in this order: (1) deferred graph save (row 5); (2) on `floor_missing` replan with the declined edge avoided and never hand off to the legacy owner at level 1, with the two respawn4 cases pinned at class 50 on v2 as the regression; (3) the plan line prints its route's edge ids so enrichment can fold planned edges from real runs into cache v3; (4) planner `avoid`/`require` directives for one search so the explorer can branch over routes; then v3, floor, cells, and the explorer with route branching.

**v2 chain stopped by owner decision, 2026-09-22.** Class 75 on cache v2 finished at **17850 ms** (control 19,450; v1 gave 17,650), so v2 offers no faster chains than v1 at the first class, as the planner-reach record explains. The chain was stopped during class 150, its processes identified by command line and its partial worlds retained, because cache v3 will refuse v2 tapes by binding and every hour of chain blocked the four C changes. Those proceed now on a released source tree: deferred save, the `floor_missing` replan with no legacy hand-off at level 1 (regression: both respawn4 cases pinned at class 50, on the seed and on v2), route telemetry on the plan line; then the planner directives and cache v3.

**Deferred save, floor safety, route telemetry, 2026-09-22.** [Record](../../movement/records/2026-09-22-deferred-save-and-floor-safety.md), build `safety-save-build-20260922-a`, 58 worlds. (1) `bot_moveGraphSaveDefer` snapshots the graph on learn and writes it at shutdown, autosave and map prepare; six reference cells identical on and off, production caches byte-identical; four-bot cost on v2: p99 **119 ms to 64 to 74 ms**, dispatch total 0.52 s to 0.06 s, every remaining worst frame a boundary search. Owner decision: default on, with a flush at each leg boundary so a crash loses at most one leg. (2) A corridor, no-path or miss decline is held by the movement owner, avoids the route's next crossing, replans and brakes; **no legacy hand-off** at level 1 (movement-type, legacy-state and off-mesh yields remain). Both respawn4 cases at class 50 pass on the seed at 19,400 ms and on v2 at 16,625, zero deaths, owner `move`; floor 36 of 36 on v2. Caveat: the fixed trip never reaches the old fatal lip, so the hold there is untested. (3) Every installed route prints a `[ROUTE]` line with its edge ids and graph identity, parsed by the enrich tool. Next: planner directives, cache v3 with planned edges, explorer route branching; then the finish-phase spreading for row 5.

**Stage 3, what a result means for the gate.** A decision tape executed by
the movement owner is a prepared plan under the lifecycle, so its trips count
toward rows 2 and 3 of the gate, on two conditions the record must show:
the bot selects the tape itself, from the map, leg, start cell and its own
phase (`level.time` modulo 200 is known to it), with the harness setting
nothing but the case; and the fallback is measured, with a refused or absent
entry returning the trip to the ranking and the trip still completing. Until
both hold, explorer times are prepared-plan results and move no row. The
phase is under the bot's control at a stand, since delaying its first command
by up to 175 ms selects the class; that is an option to measure at stage 3,
not to assume. Row 5 must be reported on the executing build with the tape
loaded, since matching runs inside the search.

**Production configuration, 2026-09-22.** The configuration the rewrite is
measured and qualified in, from here on, is the **unseeded graph** - built at
map load, no 2026-09-16 research seed - with the tape library **on** and the
parity harness **off**. Each part is a measured decision, not a preference: the
seed kills the bot on `red_ret_L1_respawn4` and `blue_out_L1_respawn4` in both
arms ([gate](../../movement/records/2026-09-22-gate-l1.md)); the harness is one optional
diagnostic and since
[the decoupling](../../movement/records/2026-09-22-graph-binding-and-decoupling.md) the
searches run under their own safety gates without it, with the same trips and
the same decisions; and the library is the prepared-plan mechanism rows 2 and 3
are attributed to. **Tapes bind to their graph.** A tape names graph cells and
edges by index, so it is refused on any graph but the one it was recorded on -
`graph=` in its header and in the library index, against `Graph_Identity()` -
and the refusal is reported as the rung `graph_mismatch` with the leg falling
back to the ranking at its first think. It follows that **the rows measured on
the seeded graph are superseded by unseeded ones**: rows 2 and 3 as reported on
2026-09-22 stand as seeded measurements and do not carry to the production
configuration, because the four explorer tapes were explored on the seeded
graph and are inert - now refused - without it. The explorer campaigns are
re-run unseeded and the rows re-measured there before the gate is reported
passed. One warning for those campaigns: unseeded the graph is built at load
and edges are learned before the release, so a start class does not fix a trip
and neither does an exact release level across worlds that differ in staging;
an unseeded cell needs repetitions and a single world is not one. **Resolved
2026-09-22** ([record](../../movement/records/2026-09-22-production-graph.md)): the unseeded
graph is now the cache `.codex-research/production-graph-20260922-a/production-graph.txt`,
identity `g1-0e5af7aba9b5e908`, 676 edges merged from the gate campaign's own
unseeded runs and loaded with `--graph`, and with it - plus the release's
learned-set reset - a start class is one completion time and one decision
sequence again (ten worlds at class 125 and six at class 75), so tapes and the
explorer bind to that identity and not to the map-load graph.

**What this does not promise.** The explorer finds the fastest chain the
live pipeline can offer from a start, not a faster pipeline; if the best
chain at class 75 is still far from the bound, that is a candidate-vocabulary
result and it is recorded as one. Tapes are bound to the map checksum and the
physics settings, and a different server frame or Pmove step invalidates
them by construction. Contention is out of scope, as the plan says.

### The transfer question, 2026-09-21: does the fast chain survive a different start class?

Owner decision after the [library-rescue result](../../movement/records/2026-09-21-library-rescue.md).
The next explorer is not built until this is answered.

**Why this comes first.** On the rescue screen's own control, the same bot
with the same library and settings completes `red_out_L1_stand` in 15,925 ms
from start class 125 and 21,125 ms from class 75. The classes differ only in
the level time at which the fixture released the runner, and each cell is
deterministic to one frame. Every frame-local intervention since 2026-09-17
has helped the slow classes and hurt the fast ones. Two readings fit that,
and they lead to different work:

- The class only sets the phase of the bot's own decisions, so the early
  commits fall into a fast chain or a slow one and nothing frame-local can
  cross over later. Then the fast chain is physically available from every
  start, and a whole-trip plan carried from the stand is the mechanism: the
  chain explorer is worth building, and the class-125 trip is its first plan
  and the reference for row 2 (it is already under the 16,500 ms bound).
- The class changes what the world does to the chain: a map entity's phase,
  the fixture's own timing, or a cadence-locked interaction. Then a prepared
  chain meets a different world from a different start and the explorer would
  fail the same way, and what decides the leg must be located and modelled
  first.

**The question.** Executed from a release pinned at classes 75, 100 and 150,
does the class-125 trip complete in about its own time?

**Method.** Native, small, one case, one leg. No source change is expected;
if one is needed to record a bot's commands, that is the first finding and
the work stops there until it is made.

> **2026-09-21: one was needed, and is made.** The recorder refused bots in
> `Arm()` by construction, and nothing else did. Under the owner's direction
> the console command still refuses a bot, and a bot is armed only by the
> corner fixture on its own release frame under `bot_routeRecord 2` - a new
> value of the existing cvar, so no new switch is added; the metadata declares
> the subject, the release level and the binding settings, and the report tool
> labels the two subjects as different kinds. **Step 1 is done** on the rebuild
> that also carries the housekeeping removal: with the recorder off and with it
> armed, the class-125 cell completes in **15,925 ms with 13 straight and 5
> state commits**, so the build is the control from here on and the recorder is
> inert on the trip. The tape is retained beside its world. See
> [the recorder record](../../movement/records/2026-09-21-bot-route-recorder.md).
>
> **Step 2 ran and the self-control does not reproduce**, so by this order's own
> reading table steps 3 and 4 do not run and neither reading of the class
> difference is drawn. Three worlds pinned to class 125 all return `tape_end`
> at **15,925 ms with 31.839 max drift and no flag**: the replay tracks the
> recording to **0.343 units and 0.017 units/sec through command 580** - the
> player's documented fidelity, inside the 0.29 to 0.35 this order required -
> and then one landing at command 583 arrives 0.283 units low and leaves with
> 20.7 units/sec less, about what one additional grounded 8 ms step costs at
> that speed. The remedy is the table's third row: a repair with a regression
> shown to fail first, then a rerun. The repair target is the 0.34 units, since
> this trip has a landing that cannot tolerate them; the bot's constant
> `delta_angles` offset, which makes its raw 16-bit command angles exactly
> recoverable unlike a human's, is the untested candidate. `route_approve.py`
> gained `--corner-class` and `--corner-level` for this, which is a tool change.

1. Record. One world on the rescue screen's control build with
   `bot_moveLibraryRescue=0`, pinned by `--corner-class 125`, with
   `bot_routeRecord=1` and `route_record` armed on the bot's client so its
   own commands are captured as a `BOTROUTE2` tape from first effective
   movement command to flag possession, the recorder's own clock. The world
   must complete in the control cell's 15,925 ms within one frame; if it does
   not, the recording perturbed the run and the record says so. Retain the
   tape beside the world, bound by hash.
2. Self-control. Play the tape with `bot_route_play` through the retained
   asset procedure ([route assets](../../movement/records/2026-09-18-route-assets.md)), from
   a release pinned at class 125, three worlds. It must reproduce 15,925 ms
   within one frame and the checkpoint drift the repaired player shows on the
   retained assets (0.29 to 0.35). If it does not, the player is the problem,
   and neither reading below is drawn.
3. Transfer. The same tape from releases pinned at classes 75, 100 and 150,
   three worlds each, arms alternated in sequence with the self-control.
   Independent search is whatever playback leaves it; ownership must return
   to the movement owner on any refusal, as the 2026-09-19 repair requires.
4. Report per world: completion time or the failure and its reason
   (divergence abort with the tape index and position, death, refused arm,
   time limit), max checkpoint drift, `corner_level_observed`, parity
   comparisons and mismatches, ownership. Twelve worlds plus the recording;
   pin misses retained and replaced as the harness does.

**Reading, declared before collection.**

| Result | Meaning | Next work |
| --- | --- | --- |
| Classes 75, 100 and 150 all complete within 250 ms of 15,925 | The class is a decision-phase artifact; the fast chain is available from every start | The chain explorer: offline search for complete trips through native rollouts from each start class, retained as plans with their entry envelopes, executed online through the prepared-plan lifecycle with independent movement as the fallback; the recorded tape is its first plan and the row-2 reference |
| A class diverges or fails at a repeatable tape index | The start class changes the world the chain meets there | Locate the frame: the tape's position and the state of every entity near it across the classes; what differs is what the explorer must model or the fixture must control. No explorer until it is named |
| The self-control does not reproduce | Player or recorder defect | Repair with a regression shown to fail first, then rerun |

#### Steps 2 and 3 revised, 2026-09-21: the decision tape

Owner decision after [step 1's record](../../movement/records/2026-09-21-bot-route-recorder.md).
The recorded class-125 tape replays to a third of a unit for 580 of 636
commands and then loses one landing by 0.28 units; the plan's third reading
applied and no transfer world ran. The repair that reading asked for is not
taken, for a reason the record's own numbers give: the trip has a landing
decided below any player's floor, so a different start class, which perturbs
the state by construction, would fail an open-loop replay whether or not the
world differs there. The instrument could not separate the two readings even
repaired. The raw-angle hypothesis stays parked as a prepared-plan fidelity
item; the player and the converter stay untouched.

The live bot reproduces 15,925 ms at every class-125 level because it is
closed-loop: it re-searches at each landing from its actual state. The
architecture executes a prepared plan the same way, validated at takeoff and
replanned when refused. So the transfer question is asked closed-loop: force
the decisions, not the commands.

**The instrument.** The forced-candidate override in `bot2_move.c` already
runs after `StatePreferred`, the last thing that can change the winner, and
before the route is installed, and it can only select a candidate the judge
admitted; it forces one named candidate at one named commit. The decision
tape generalises it and keeps both properties:

- A tape is a list of the class-125 world's committed decisions, built
  read-only from its retained `[SPIKE]` and `[MOVE]` telemetry by a new
  script beside `movement_plan_coverage.py`, with a unit test on a
  hand-built log. Each entry carries the commit's root position and cell,
  its `kind`, the state plan's edge when it is a state candidate, and the
  straight candidate's printed aim and run-up fields (`d0 r0 d1 r1`) and
  hop count otherwise. The preference-localisation work reconstructed the
  same decisions from the same lines, so the fields are known to be there.
- One default-off cvar names the tape file, loaded the way `bot_moveCorridor`
  loads its polyline. Unset, the override is exactly today's and the world
  is the control.
- At each judged candidate set, the diagnostic looks for the first tape
  entry at or after its cursor whose root lies within 64 units of the live
  root, so an extra or missing commit does not misalign the rest. If an
  admitted candidate matches that entry (same kind; same plan edge for a
  state candidate; aim and run-up equal as printed for a straight one) it
  is selected as the override selects, the cursor advances, and the entry
  is a **hit**. If the entry's candidate was judged and not admitted, the
  entry is a **refused miss** with that outcome. If no candidate matched,
  it is an **absent miss** with the nearest candidate's kind and aim gap.
  If no entry lies near the root, the ranking decides and nothing is
  recorded. Nothing changes admission, physics, cadence or ranking weights.
- Telemetry after the search returns, never inside it: one line per hit or
  miss with the entry index, the kind, the outcome and the cursor.

**Verification before collection, on a fresh build, sources then frozen.**
Tape unset: one class-125 world reproduces 15,925 ms with 13 straight and 5
state commits. Tape set to its own world's decisions at class 125: one world
reproduces the same time and commits with every entry a hit. That is the
closed-loop self-control; if it does not pass, the matching is wrong and
nothing else runs.

> **Passed 2026-09-22, at the fourth attempt**
> ([record](../../movement/records/2026-09-22-decision-tape.md)). Tape unset **15,925 ms, 13
> straight and 5 state**; the tape from that world's own decisions **15,925 ms,
> the same 13 and 5, all 13 entries hit and no miss**. Zero deaths, movement
> owner throughout, 667 parity comparisons with zero mismatches in both. The
> five failing runs are retained: they found that a planned edge pair is
> offered at two run-ups so edges alone do not identify a state decision, that
> two candidates for one edge pair can share every printed first-hop field and
> fly different second hops (so `d1` and `r1` joined the straight search's
> commit line, which alone had printed only the first hop), that an objective
> change calls `Move_Reset` mid-trip so the cursor must start at the release
> instead, that a commit printed on the release level belongs to the previous
> trip, and that a search admitting nothing has made no decision and must not
> consume an entry, that a decision cannot be named by its printed aim at all,
> and that a tape must stop at the objective or it carries the return leg's
> decisions too.
>
> **Collected 2026-09-22, and the answer is not one row of the table.** 18
> worlds, all complete, no pin misses, every triple identical.
>
> | Start class | Control | Tape | Change | Hits | First miss |
> | ---: | ---: | ---: | ---: | ---: | --- |
> | 75 | 21,125 ms | 21,125 ms | 0 | 1 of 11 | entry 1, **refused** |
> | 100 | 19,375 ms | **18,925 ms** | **-450** | 2 of 11 | entry 2, **absent** |
> | 150 | 17,575 ms | **17,025 ms** | **-550** | 1 of 11 | entry 1, **absent** |
>
> Entry 0 - the stand's state chain on edges 50732/26663 - transfers from every
> start. The sequence then stops at the second or third decision for a
> class-specific reason: **class 75 on row 2** (offered and refused, twice, at
> the same level in all three worlds), **classes 100 and 150 on row 3**
> (absent, and their roots return near entries 8 to 10 later in the leg and
> still match nothing). **Row 1 is refuted**: no class comes near 15,925 ms or
> hits more than 2 of 11, so the class-125 sequence is not available whole from
> any other start and is not the explorer's first plan. Forcing the prefix that
> is available is worth 450 ms at class 100 and 550 at class 150 and nothing at
> class 75 - a diagnostic result under forced selection, moving no gate row,
> but evidence that the ranking's own choice at those first decisions is not
> the best available from those starts.

**Collection.** Classes 75, 100 and 150 by `--corner-class`, three worlds
each, the tape on, arms alternated in sequence with a tape-off control per
class. Per world: completion time, hits, refused and absent misses by entry,
the first miss with its frame and root, rows 1 safety, parity with a
non-zero count. Pin misses retained and replaced.

**Reading, declared before collection.** It replaces the tape-replay table's
second and third rows; the first row's conclusion is unchanged.

| Result | Meaning | Next work |
| --- | --- | --- |
| Every class completes within 250 ms of 15,925 with all or all but one entry hit | The class is a decision-phase artifact; the fast chain is available from every start | The chain explorer, with the class-125 decision sequence as its first plan and the row-2 reference |
| A repeatable first **refused** miss at the same entry across a class's worlds | The chain is reachable from that start but that hop is not admissible from the state it arrives with | Locate the frame: the live root, speed and heading against the tape's, and the entities near it, across classes. What differs is what the explorer must carry in its entry envelope, or what the fixture must control |
| A repeatable first **absent** miss, the root never coming near later entries | The decision sequence is not reachable from that start; the trip leaves the class-125 corridor before it | The explorer is still indicated but must search per start class, and the plan library is keyed by start state, not by map position alone |
| The self-control does not hit every entry | Matching defect | Repair with a regression shown to fail first, then rerun |

**What this does not establish.** Forced decisions are a diagnostic, not a
prepared-plan execution, and move no gate row; tapes count toward the gate
only under the conditions of "Stage 3, what a result means for the gate". A
trip that completes under the tape says the decisions are admissible from
that start, not that an explorer will find them or that they are robust to
contention. One case, one leg, one map.

**Housekeeping carried with this order.** The library-rescue switch and its
code (`bot_moveLibraryRescue`, `Graph_AdmittedStrafe`, the rescue pass in
`StraightSearch`) are removed, as the third-revision order required of a
rejected arm; the record is their evidence. Parking it would start the
switch list the README's principle 7 exists to prevent. **Done 2026-09-21**,
in the same rebuild as the recorder change, with `MOVE_RESCUE_MAX`,
`RescueCensus`, the rescue telemetry line and the `rescue=` field in the
committed sequence removed with it. The class-125 control cell reproduces to
the millisecond and to the commit count, which is the check that the removal
changed nothing.

## Current method and milestones, 2026-09-19

The [refined method](../../movement/records/2026-09-19-refined-method.md) and
[architecture](../../movement/ARCHITECTURE.md) supersede the historical next-action language
in the phase records below. Independent movement is the base capability;
prepared plans are optional candidates under one execution/recovery lifecycle.
Neither more human demonstrations nor general rejoining blocks this work.

| Milestone | Required result | Status |
| --- | --- | --- |
| Matched repaired baseline | Explicit state/physics/resource/cadence binding and observed search activity | Done 2026-09-19: `review-fix-build-d` verified against the checkout, `red_out_L1_stand` at `--edges --parity 256` with observed commits, 6 repetitions |
| Guided expressiveness diagnostic | Search generates a connected native sequence in the human corridor without replaying its commands or resetting state | Done 2026-09-19: [the guided corridor](../../movement/records/2026-09-19-guided-corridor.md), refusal classification corrected after review. 20,500-20,725 ms guided against the human's 14,800 on the same line; the corridor is not the limit, and the losses at the joins are classified into a correct pit refusal, a selected-path rejection and an unsupported simulation |
| Independent connected search | Playback and human guidance off; repeated complete stand runs toward roughly 15 seconds, plus displaced/interrupted cases | Acceptance moved 2026-09-21 to rows 2 to 4 of the [level-1 exit gate](#level-1-exit-gate-2026-09-21); 15 s is the reported reference, not the gate. History of the row follows. Open, with another measured step: actual-landing successor proposals give eight-pair **mean 17,943.75 versus 18,840.625 ms**, median 18,125 versus 18,512.5, six faster/two slower. Best 16,450 repeats three times; it is not the median. Both 36-case floors pass. Search cost rises; 15 seconds and runtime qualification remain open. [Successor record](../../movement/records/2026-09-20-successor-aim.md). Exit-speed 500 is **confirmed** against 2000 at matched verified starts ([pinned confirmation](../../movement/records/2026-09-20-exit-speed-pinned-confirmation.md)): four declared start classes, six matched pairs each plus a three-pair anchor, 53 timed worlds, one cell incomplete. Each cell has one distinct completion time; 500 wins 1,475 ms at class 150 and 525 at class 125, ties at 100 and **loses 1,025 ms at the slow class 75**, six pairs of six. Declared-weight improvement **807.8 ms** (17,394.4 versus 18,202.2 ms), equal-weight 243.75 ms; search cost unchanged. Floors 36/36 unpinned at 500 plus both seeded regressions. The start level is a class (`corner_level mod 200`), pinned by the new `--corner-class`. That class's regression is then localized to one frame: [the slow class's selection failure](../../movement/records/2026-09-20-slow-class-selection-failure.md) shows 12 admitted candidates at relative 4,775 ms whose top three score 0.1 apart, and a forced-candidate diagnostic (`bot_moveSelectNth`/`bot_moveSelectCand`, default off) isolates the frame: under one fixed policy, control 20,100 ms, forced cand 5 20,925, forced cand 1 18,775 - one frame worth 2,150 ms with the ranking taking the middle. Repeated with the policy fixed at 500 ([the exit-speed-500 repeat](../../movement/records/2026-09-20-select-override-exit-speed-500.md)), the same frame is worth **2,300 ms** and the ranking takes the **worst** of the three: control 21,125 ms, forced cand 13 19,950, forced cand 1 18,825, 12 cells of 12 over 34 worlds; that frame carries most of the class-75 regression - the choice between the two weights' candidates is worth 825 ms under the 2000 policy and 1,175 ms under 500 - and the same candidate moves at most 200 ms between the two policies. The horizon is then priced from the realized continuations: the ranking by distance still to run at common elapsed-time endpoints is stable only from **frame + 7,000 ms** under both policies, roughly half the remaining leg, and at frame + 5,000 ms the true winner ranks **last**. That is an oracle bound on a bounded rank, not the rank itself, and its CPU cost is unmeasured. Widened to three more frames of that leg ([the frame survey](../../movement/records/2026-09-20-selection-across-frames.md), 33 cells of 33): every frame carries 1,525 to 2,300 ms, the ranking is worst or near-worst at 4 of 4, and at commit 5 - four admitted, **all four forced** - it takes the slowest while the best has the lowest score and two candidates share a point in progress and carried speed yet differ 600 ms. Then widened to a second start class ([class 150](../../movement/records/2026-09-20-selection-second-start-class.md), 30 cells of 30, three commits forced **exhaustively**): there the ranking commits the **fastest** candidate at 3 of 3 and it is the score maximum every time, so the failure is **class-specific**. The `kind` field names the difference - at class 75 a planned **state** candidate executes over a higher-scoring straight one - and removing that preference ([the preference arms](../../movement/records/2026-09-20-state-preference-costs-the-slow-class.md)) moves class 75 from 21,125 to **17,975 ms** while class 150 regresses 150, with floors passing unpinned at that setting (36/36, both seeded regressions). The full four-class grid then **refutes removal** ([the grid](../../movement/records/2026-09-20-preference-removal-refuted.md), 12 cells of 12): with the preference off, class 125 regresses **+3,025 ms** and class 100 **+1,775**, against -3,150 at class 75 and +150 at class 150, for a declared-weight mean of 17,394.4 ms on against **18,557.3 off, a 1,162.9 ms loss**, and 450 ms under equal weighting; search cost is unchanged (pooled in-trip rollout median 36.50 against 35.05 ms) and all four prefer=1 cells reproduce the confirmation 500 column exactly. `bot_moveStatePrefer=1` is confirmed. The preference is right at three classes of four, so the target is a **conditional** preference at the decision, which re-elevates bounded continuation ranking against its priced 7,000 ms horizon. That decision is then located and priced. [The localisation](../../movement/records/2026-09-20-preference-localisation.md) reconstructs the judge's own ranking from the retained `[SPIKE]` rollouts - 378 commits in 24 worlds, 141 exact, **21 preference firings and none unexplained**, none in a preference-off arm - and finds the same decision at three classes: commit 2, about 2,950 ms in, on edge 36420. Neither the score gap nor domination separates the class where the preference loses; the **speed the bot arrives with** does, 99.1 units/sec against 250.0, 250.1 and 242.1. [Forcing that commit in both directions](../../movement/records/2026-09-20-preference-decisive-commit-priced.md) (36 cells of 36, 93 worlds, all six controls reproducing to the millisecond) prices it: the state plan is wrong at class 75 from both sides (**1,900 ms** saved by holding the judge's winner, 1,425 lost by forcing the plan into the off trip), right at class 125 from both sides (**5,300 ms** lost by holding the winner), and at class 100 the two sides **contradict** - holding the winner is 150 ms faster while forcing the plan into the off trip saves 3,050 and is the fastest class-100 trip measured. The frame is worth seconds, its value is policy-dependent and not additive, and a rule declining the preference only at the class-75 decision would be worth **122.6 ms** weighted and 475 equally weighted, while the best of the four policies measured here chosen per class is 340.3 weighted but **1,106.25 equally weighted** - the same combination priced two ways, so the size of this prize is mostly the borrowed start distribution, and best of what was tested is **not a ceiling** on selection. Owner direction 2026-09-20: **park the preference branch with `bot_moveStatePrefer=1`**, restore meaningful forecast/execution parity comparisons before any deeper forecast, and put the next speed work into **offering better continuations at classes 150 and 125** - 77 of the 93 declared weight, and the class where selection already commits the fastest candidate offered. 500 is a research value, not installed; 15 seconds and runtime qualification remain open. **Parity is no longer vacuous** ([diagnosis](../../movement/records/2026-09-20-parity-zero-comparisons.md), [repair](../../movement/records/2026-09-21-parity-repair.md)): every build was refused because the walk it forecasts ends at the objective flag (119 of 119 parity refusals), and the armed prediction shared its frame buffer with every search rollout. With both repaired the floors give 36 of 36 plus both seeded regressions, zero deaths, movement owner only, **19,413 frames compared and zero mismatches**, 41 of 63 tallies covering a committed sequence; restoring the shared buffer alone returns 935 `after_state` mismatches, so the zero is a measurement. `compared` is a tally-carried lower bound, the harness runs under `bot_moveParity` which only the bench sets, and nothing is installed |
| Runtime qualification | Search enabled independently of parity diagnostics; measured per-frame and multi-bot CPU budget, valid result lifetimes, no controller-induced idle wait | Open; now row 5 of the [level-1 exit gate](#level-1-exit-gate-2026-09-21) |
| Prepared-plan integration | Applicable plans compete including entry cost; refusal/departure resumes independent movement under the same owner | Open; promoted 2026-09-21 to the level-1 mechanism, see the gate's [plan-coverage diagnostic](#the-plan-coverage-diagnostic), which [reported](../../movement/records/2026-09-21-plan-coverage.md) that the library admits an edge at 56.7 percent of waiting runs, that no waiting state is unvisited, and that the campaign's own caches are the seeded library to within four edges |
| Intermediate entries and contention | Connection plus continuation validated from actual state; measured perturbation support and interference outcomes | Later, not a prerequisite. First perturbation measurement 2026-09-24, [knockback fixture](../../movement/records/2026-09-24-knockback-fixture.md) (`bot_moveKick`, native `G_ApplyKnockback`, 8 kicked arms and 3 controls). Walk kicks recover at medians under 1.3 s. Owner scope: a push straight into a pit is not a case to solve, and 40 of 46 deaths are that. In scope: 6 deaths after an off-course landing (brake plus refused searches, then momentum carries the bot over the edge), 5 stalls on a mover floor and 1 onward hold after a kicked hop. [Knock recovery](../../movement/records/2026-09-24-knock-recovery.md) (`bot_moveKnockRecover`, on by default, owner 2026-09-24): a hit ends the committed sequence, and a rolled-out air-control landing that stands on the graph is committed. No arm had a death after landing with it on, most direct deaths were saved too, and 108 unkicked worlds gave zero false hits. It exposed and fixed a forecast leak: rollouts on dirt spawned live landing-effect entities, contaminating Dantooine searches. The lift and mover gap is the family left; [lifts](../../movement/records/2026-09-24-lifts.md) makes a resting lift floor and adds rides; the stall ends by riding down and the kicked arm is 36/36 |

Keep graph proposals distinct from executable sequences. Offline and online
search share maneuver/forecast contracts and actual successor-state semantics,
with different compute budgets. The current explorer samples edges, the graph
uses approximate momentum, and the human tape player is a research harness;
none alone implements the combined target.

The approximately 15-second goal is for the matched stationary no-Force stand
comparison, not all arbitrary positions and not the 12.9-second tape's different
start. Report completed repetitions, median and tail time, all failures, spend,
parity, ownership and CPU cost. A single best time is not reliable attainment.
The earlier strict sub-15 challenge remains a separate timed claim. Do not
trade the safety gate for speed or silently change physics/cadence to meet it.

Before native collection, declare the comparison settings and repeated cases.
Use the existing floor/seeded arm and named chain-entry/onward-held failures;
count setup failures separately and complete missing cases. Guided results,
unguided quality, production cost and prepared-route execution each need their
own verdict. Match repaired controls before reusing reroute/tap causal claims.

The diagnostic's directed work, with its own paired control and arms on the
same case, before anything else in the independent-search milestone:

- [x] Landing frame auditable: every branch that can take one now names
      itself, including three silent exits inside `StraightSearch`. 40
      last-hop landings over three guided runs, 0 silent; both named reasons
      (`straight_too_short`, `off_path_index`) are path causes at 352-446
      units/sec. Inert - the guided arm reproduces its reference runs to the
      millisecond and the commit counts - after moving the printing out of the
      search, where it had cost 2,000 ms by a mechanism still unresolved.
- [x] Selected-path rejection measured on a repaired control.
      `bot_moveReroute=1`, eight sequential repetitions per arm: joins kept on
      the landing frame 61% against 53%, units/sec lost 1,527 against 1,591,
      **completed stand time unchanged** (median 20,362 against 20,150).
      `=3` worse on every measure. Speed kept at a join is not time saved.
      Review then found two defects in the pass itself - the installed route
      could be another candidate's, and the continuation forecast was not the
      one executed - both now fixed, neither measured. The arm therefore does
      not settle whether the rejection is addressable; it settles that this
      pass, as built, did not shorten the trip.
- [x] Re-measured `bot_moveReroute=1` against the control on the corrected
      pass, nine sequential alternating repetitions (one dropped from both
      arms for an RCON fixture refusal). **Indistinguishable from the
      control**: median 19,550 against 19,600, mean 19,669 against 19,569,
      five of eight pairs faster, per-pair deltas -900 to +1,175. The pass
      installs 11 routes against the defective pass's 18 and both new guards
      stayed silent. Correcting the defects neither won nor lost; the
      continuation's validity is not what stands between kept speed and saved
      time. The joins-kept metric is unqualified - 61% is not reproducible
      from the retained logs and one reading of it reverses the sign.
- [x] Review the retained momentum measurement with reproducible leg boundaries:
      [corrected record](../../movement/records/2026-09-19-what-the-kept-speed-buys.md).
      Keep only complete 500 ms windows after fixture placement and before
      the last noncarrying play sample. Result: 58 pooled landings, not 67;
      33 airborne / 25 grounded at +25 ms. Grounded mean speed drops
      445.4 -> 342.9 in 25 ms, reaching 257.3 by 100 ms. This is an observed
      association, not a universal causal or heading-exclusion result.
- [x] Check trace independence: r1/r2 have identical 760-row safe-window
      momentum traces. Two distinct traces yield 39 landings, 22 airborne /
      17 grounded. Report both pooled and distinct-trace summaries; do not
      call three launches three independent trajectories.
- [x] Classify landings by committed plan length. A one-hop plan ends at
      hop0. Within-chain continuation is 26/26 pooled (18/18 distinct);
      sequence-end continuation is 7/32 pooled (4/21 distinct).
- [x] Recount grounded actions: 13 committed / 5 admitted nothing / 7 no
      recorded straight search, or 10/3/4 with duplicate trace removed. Grounded
      commits all select 24-unit run-up; two have an admitted tap-now proposal.
- [ ] Re-establish any coarse distance-bin/heading conclusion using a
      reproducible outbound-only method. Earlier correlation, fast/slow and
      heading-exclusion claims are withdrawn, not qualified by the new trace.
- [ ] Track physical next-frame airborne state, committed plan length and
      run-up; a commit alone is not a tap or an ownership measurement.
- [x] Explain all four missing-search states with matched replay telemetry:
      `off_path_index`, no cadence/retry delay. A final committed landing on
      an admitted future chord retained the takeoff index. Reconcile only a
      failed lookup at final touchdown, keeping ordinary walking guarded.
      Four actual-C regressions pass and the exact baseline fails. Fresh
      build floor 36/36 passes; sixteen paired timed runs complete. The
      corrected median is slower by 787.5 ms and mean by 212.5 ms: no speed
      optimization claim. [Record](../../movement/records/2026-09-20-landing-search.md).
- [x] Classify the eight grounded commits without a tap alternative:
      23 selected-path refusals and 29 trigger-contact unknowns across 52
      zero-run-up candidates. No hazard or harmless-contact verdict follows
      from the trigger bit alone.
- [x] Measure the short-straight geometry and offer arc-aimed candidates at
      observed final landings. Actual-C gate/path tests pass; ordinary walking,
      near-goal and invalid-path exclusions remain. The isolated fix's gain
      is marginal; the combined reroute/exit-credit configuration establishes
      the repeated speed win above. [Record](../../movement/records/2026-09-20-speed-win.md).
- [x] Propose second-hop headings from measured first landings, then reroll
      full sequences from the original native root. Eight counterbalanced
      pairs improve mean, median and maximum; six improve and two regress.
      The 8-unit run-up screen regresses and is reverted. Reroute/credit
      component separation establishes no replacement. [Record](../../movement/records/2026-09-20-successor-aim.md).
- [ ] Next: improve slower successor outcomes and useful connected continuation
      toward 15 seconds, preserving measured references. Avoid repeated
      first-flight forecast work and qualify CPU/positive parity. Identify
      unknown trigger entities separately; preserve every admission gate.
- [ ] The 36-case floor qualifies behaviour and not parity: with `--parity 256`
      it records **0 comparisons** over all 36 cases (6,475 on the diagnostic
      build, 6,614 on the corrected one, all refusals). Find out why the
      harness never compares a frame, and either restore comparison or stop
      reporting "no parity mismatches" from a floor.

Bounded [repair-review follow-ups](../../movement/records/2026-09-19-repair-follow-up.md),
required before full tape lifecycle qualification and independently actionable:

- [ ] Release or preserve ownership transactionally when loading over playback.
- [ ] Require actual expected phase transitions in the ownership probe; replace
      the vacuous valid-arm result with retained focused evidence.

## Immediate review repairs, 2026-09-19: landed

The [review repair work order](../../movement/records/2026-09-19-review-fixes.md) is closed by
[the repair record](../../movement/records/2026-09-19-review-repairs.md). Each repair carries a
regression that was shown to fail against the defect before it passed:

- [x] Reset complete candidate results between searches, including extension
      flags. 0 stale slots on the repaired build against 372/200/396 on a
      paired defect build, over three extension-armed cases.
- [x] Preserve the first tape command's actual physics duration. Eleven
      converter timing regressions; latent in the three retained assets, whose
      first command is the 8 ms playback was substituting.
- [x] Bind checkpoint evaluation to the corresponding executed command. Twelve
      route runs reproduce every retained route time and drop max drift from
      23.8-37.8 to 0.29-0.35 on the controls.
- [x] Restore AI ownership when playback refuses to arm.
      `tools/route_ownership_probe.py` passes on the repaired build and fails
      on the paired defect build at both refusals.

The tape and asset formats are versioned (`BOTROUTE2`, `BOT_ROUTE2`) and the
older tag is refused by name; retained assets and approvals are kept as
historical evidence and the rebuilt ones live beside them. Six standard floor
attempts, three per build, had no observed deaths or parity mismatches; one
repaired attempt completed 35/36 because of a setup refusal, the other repaired
attempts 36/36. The seeded level-1 arm is 12/12. Experimental defaults are
unchanged. The older chain-entry/onward-held defects and the phase
gates remain open. [HANDOFF.md](../../movement/HANDOFF.md) orders resumption.

## Benchmark gate

`bot2_movebench.c` gains a fixed case list under `tools/fixtures/movement/`:
a full initial state (position, velocity, heading, latch, Force, remaining
Speed window), a goal, a spend policy naming which edge templates the level
permits, and a pass condition. Cases are added from observed failures, one
case per failure, and every build runs all of them.

Safety is binary: no green case may turn red, where red is death, a stall
past the case's bound, or a forbidden spend. Improvement is measured: time,
reliability across repetitions, preparation cost. A change that fixes no
case and improves nothing measurable is not taken. Aggregate campaigns may
discover cases; they never accept rules.

The first set is Kejim, both teams, both legs, three levels, from the stand
and two respawns each: 36 cases. Baselines are matched-resource runs of the
snapshot build, recorded once; where the old runner cannot run a case the
legacy machine is the baseline.

### Cheap comparisons (owner order 2026-09-25)

Verification must not be the bulk of the work. A comparison of two settings
or builds is, by default:

1. **Pinned, deferred.** `--corner-class 50` and `150` (the bench defers by
   default; [record](../../movement/records/2026-09-25-deferred-pin.md)). One world per case,
   four at a time; pairs are matched per case and class.
2. **Twins skipped.** `--skip-twins` on both arms: return legs repeat their
   outbound twin on every fixture map except Kejim's stands and Taloraan's L3
   respawns, which the table keeps (1,913 of 1,916 retained pairs identical;
   the eight maps of 2026-09-27, 1,026 of 1,026,
   [audit](../../movement/records/2026-09-29-twin-audit.md)).
   216 cases a class become 118; a map-class runs in about 40-60 s.
3. **Control reused.** When the change cannot touch the control's path (a new
   cvar default off, a tool change, a treatment arm's settings only), do not
   re-run the control on a new build: run one map-class of it on the new build
   and require every shared pair identical, then pair the treatment against
   the retained control. Any difference means the control is re-run in full.
   Say in the record which arm was reused and the identity check's numbers.
4. **Scope to the path.** Run the maps and levels whose route the change can
   reach; add the rest only for a default change.

Gates, floors and acceptance keep every case, both twins, and unpinned starts
where the gate says so. A comparison that finds a candidate still goes to
the gate before it becomes a default.

## Phase 0: scaffold

- `movement_t` state, `Move_Reset`, `Move_Command` that only walks using the
  retained corridor checks and the navmesh waypoint (the only phase in which
  the mesh is on the critical path).
- `Bot2_ExecuteMovement` calls it ahead of the legacy machine when
  `bot_moveRewrite` is set. Everything else unchanged.
- Gate: the walk-only bot completes every benchmark case at level 1 with no
  death, however slowly. This establishes the floor every later phase must
  not drop below.

Landed 2026-09-15: `bot2_move.c` holds the state, the reset, the walk
executor and the parity harness, with one step function that the live
frame and the forecast both call. Floor 35 of 36 with zero parity
mismatches; the red was the legacy jump's hand-off landing, which no
walk-frame rule reaches and phase 1 replaced. The snapshot baseline was
recorded once, 21 to 29 s a case. Method and evidence in
[EVIDENCE.md](../../movement/EVIDENCE.md#phase-0-scaffold).

## Phase 1: composition spike

Before any graph exists, prove the hardest assumption on one automatically
discovered sequence on Kejim: run-up, tap jump, moving landing, second jump
or corner, useful onward travel. Discovery proposes takeoffs from the live
state and rolls them out with `MoveControl` as the command source; the same
policy executes live. Run it from a handful of starts that differ in
position along the lane, heading and wishspeed.

Two gates, kept apart so neither can hide the other:

- **Parity, zero mismatches.** Forecast and live agree exactly, at every
  movement boundary across the whole committed horizon, on commands,
  controller state, movement state and whatever resource state the forecast
  models. This is the standard the archived transition protocol met; a
  discrepancy is diagnosed, never absorbed by a tolerance.
- **Composition, every declared start.** Each declared start obtains a
  sequence, executes it with no death and no forbidden spend, and completes
  the second maneuver with useful onward travel. A refusal on a declared
  start is a failure, so refusing everything cannot pass.

A run binds the binary, map checksum, settings, cadence, initial state and
simulation exclusions. Anything outside that binding invalidates the run;
it is rerun, not counted and not dropped.

A failure blocks phase 2 until it is classified. It does not by itself say
the graph design is wrong.

What the spike must report, because the graph is designed from it: which
state dimensions actually decided whether the second maneuver was reached.
Those, and only those, become the entry envelope and the planner state in
phase 2. Cohorts, frozen envelopes, held-out starts and negative controls
are phase 2 concerns, introduced only once there is a population of edges
to trust.

Landed 2026-09-15: discovery, execution and parity share the one step
function; `bot_moveSpike` rolls out 36 two-hop candidates from the live
state and commits the fastest. Both gates met on all ten declared starts
with zero parity mismatches. The finding the graph is designed from: the
level-1 tap's entry envelope is position along the lane, not speed. Method
and evidence in [EVIDENCE.md](../../movement/EVIDENCE.md#phase-1-composition-spike).

Open from the spike: the forecast refuses near triggers and items, so a
sequence that passes a pickup cannot be validated yet.

## Phase 1b: forecast extension

- Extend `bot2_forecast.c` to roll out with Speed active, with Speed expiry
  and levitation drain applied between steps and regeneration blocked while
  a power is active. Trigger, item and moving-world exclusions stay.
- Gate: forecast-versus-live agreement with Speed active and expiring on the
  phase 1 sequence, measured before any level-2 edge is trusted.

Landed 2026-09-15: `modelForce` on the forecast request applies the
game's Force update per frame, so Speed expiry, levitation and
regeneration roll out; 14 of 14 starts with zero mismatches, Speed active
and expiring. Method and evidence in
[EVIDENCE.md](../../movement/EVIDENCE.md#phase-1b-forecast-extension).

## Phase 2: graph nodes with walk, corner and jump edges

- Preparation runs the layered flood fill from the BSP with the retained
  traces, seeded from spawns and stands and re-seeded from rollout landings,
  and builds nodes with walk, corner, hop and strafe-jump edges carrying
  the entry and exit envelopes the spike showed are needed. Cached per map
  checksum. The navmesh, if present, only supplies the search heuristic and
  off-mesh hints.
- Planner searches to the objective and commits the prefix; the executor
  validates each committed jump from the live state.
- Gate: level 1 and 2 cases complete with strafe hops on straights, corners
  are not stopped arrivals, no death, and times beat the matched baseline.
  This is the milestone that shows levels 1 and 2 share one law and one
  code path while their edges differ.

Landed in steps on 2026-09-15 and 2026-09-16: node discovery from the
BSP at map load (`bot2_graph.c`), gap edges discovered at corridor
refusals, the A* planner with link permits as the default, and straight
hops chained from landings (two-hop chains, the landing-frame tap, hops
aimed one hop-length along the path, the pruned search, arrivals at the
flag). The floor stands at 36 of 36 with zero parity mismatches and the
rewrite owning every frame, 6.8 s under the snapshot baseline on average:
the stand cases 20.5 s, respawn-5 14.5 s, respawn-4 17.5 s. The gate is
met on hops, death and time at level 1; the level 2 cases ran at the
level 1 wishspeed since Speed is a phase 3 edge, so the two levels sharing
one law is not yet shown with different edges; the corner clause has no
dedicated measurement, the walk braking only at the objective and at an
uncommitted edge's takeoff. Every step's method, defects found and
evidence are in [EVIDENCE.md](../../movement/EVIDENCE.md#phase-2-graph-nodes-with-walk-corner-and-jump-edges).

Open from the planner, in order: candidates rolled out from a start in
the home flag's pickup box are refused at frame 0 until the walk leaves
the box, which the forecast's contract on touched items would have to
change to fix; the candidate set is not a lever worth taking, since 52 of
167 one-hop commits in prune-b beat an admitted two-hop from the same
search and the saving would be 14 percent of candidate frames; straight hops as learned edges, whose
replay through the planned-edge search (a fixed takeoff, the 128-unit
onward gate) is unmeasured; preferring a measured edge to a guessed hint (a pad's guessed
time chose a pad over a walk 2.7 s faster before the ramp links existed);
re-seeding from rollout landings and the seen-but-unreached report; moving
discovery off the parity harness's Pmove boundary, since it still needs
`bot_moveParity`. The seed set is entity classes and the hints are the only
navmesh input, so the phase 4 no-mesh run has the same cells and no pad
edges.

## Phase 3: resource edges and the state-space planner

Three steps in a fixed order, decided by the owner on 2026-09-16: the
Speed edge (landed), then the state-space planner proven at level 1 and
then at level 2, and only then held jumps. Each level must pass with the
full stack before the next is attempted, because each level adds a layer
to the one below and a defect found at the higher level cannot be
attributed if the lower one was never shown clean.

- Step 1, the Speed edge: a 50 Force edge that raises the wishspeed of
  every later edge for the power's window, taken by the plan and confirmed
  by the game. Landed; see below.
- Step 2, the state-space planner. A strafe run is a chain: the landing
  speed and heading of one hop are the launch state of the next, so the
  value of a hop depends on the state it leaves for the rest of the trip,
  exactly as a racing line does. Today the chains are found greedily at
  each landing with a two-hop lookahead, and the graph search prices
  links at the walk's speed and knows nothing of state. This step makes
  the planner's search state the node plus the state dimensions the
  rollouts have shown matter (speed along the lane, heading, whether the
  next tap is on the landing frame) plus Force and the Speed window, with
  edges recorded as validated with their entry envelope and exit state,
  and the search run to the objective over that state so the chain is
  chosen as a whole. Straight hops become learned edges here, with their
  states, which is the phase 2 open item. Gate, in two parts that are not
  skipped: first every level 1 case completes through the state-space
  plan with no death, no forbidden spend, zero parity mismatches and no
  legacy owner, and is no slower than the greedy chains; then the same at
  level 2, with the Speed edge inside the search so the planner knows
  what Speed buys. Level 2 is not attempted until level 1 passes.
- Step 3, held jumps: held-jump edges with Force cost, the no-regen window
  and the spend policy in the search state; the sustained-ascent jump
  model from PHYSICS in the rollout policy. Not started until step 2
  passes at both levels. Owner, 2026-09-27: ordered last on the
  [roadmap](#roadmap-owner-2026-09-27), after the CTF brain's Force budget
  exists. Gate: level 3 cases; a run that starts at Force
  100 and one that starts at Force 12 both complete, and the second
  degrades to tap edges without an owner change or a stall. The exact-12
  return cases from the archive enter the set here as measured
  constraints with an open feasibility question; see LESSONS.

Landed in a first step on 2026-09-16: the Speed edge. The plan buys Speed
on a grounded frame the walk owns when the case's spend policy
(`bot_moveSpend`, passed per case by the benchmark) permits it, the pool
affords it and the path left is worth the window; the game activates the
power after that frame's Pmove, the next frame confirms it by the active
bit, and the forecast applies the same activation to a command that
carries it, so a purchase inside a horizon is compared rather than
tolerated. Every later edge reads the live wishspeed: the walk, the hop
aim model and the rollouts. Both final runs are 36 of 36 with zero parity
mismatches and the rewrite owning every frame; the level 2 and 3 cases are
5.5 s faster than the phase 2 arm on average, every one of them faster,
and level 1 is unchanged. Three defects the faster runs exposed were fixed
on the way and are part of this step: a walk permit is revoked by any plan
decision after its grant, the walk brakes when its momentum chord runs
onto a drop or a hazard, and a flight arrives only by the game's own touch
test. Method and evidence in [EVIDENCE.md](../../movement/EVIDENCE.md#phase-3-resource-edges).

Landed in a second step on 2026-09-16, at level 1: the state-space
planner. Completed straight plans are learned as strafe edges with the
state their rollout measured (entry speed and heading, landing-frame tap,
exit speed and heading, flight time), folded per cell pair and speed
band; `Graph_PlanState` searches cell and state (banded speed, a landing
heading octant, the landing-frame bit, the Speed window) with walk links
priced by the walk's own law from the state's speed and strafe edges
admitted only inside their entry envelope; and the planned chain's next
hop joins the straight search as candidates aimed at its learned landing
when the bot is at its takeoff, ranked by the one judge with the search's
own. A strafe chord is a flight, not a walk: validated at its takeoff,
refused and replanned at once when not flown. Learned edges cross worlds
through `tools/movement_graph_merge.py` and the benchmark's `--graph`
seed. The gate's first part is met: seeded with the level-1 edges of one
learning run, both level-1 arms are 12 of 12 with zero parity mismatches,
no legacy owner and no spend, 173 and 652 ms faster than the greedy
chains of the Speed edge arm; the whole fixture unseeded on the same build
is 36 of 36 with zero mismatches and 90 ms faster. Five defects the runs
exposed were fixed on the way and are part of this step: the fall judge
measured from the root, a validation search that replanned on every
failure, the walk's target measured from a chord's takeoff, a chord joined
from the side, and a chord refusal nobody searched. Method and evidence in
[EVIDENCE.md](../../movement/EVIDENCE.md#the-state-space-planner-at-level-1-2026-09-16).

Open from step 2 at level 1, in order: the state plans are 28 of about
145 commits a run and the gain over the greedy chains is inside a hop of
the repetition spread, because a learned edge is admitted only from the
cell it was learned at and the landing that reaches it rarely repeats the
state; a second round of seeding is unmeasured. Then level 2, the gate's
second part: the Speed edge inside the search (the walk law already reads
the window; the purchase is not yet an edge the search takes), so the
planner knows what Speed buys and the second purchase at level 3 sees the
trip left. The gap lookahead's permits still block straight hops for
their range (1.0 to 1.8 s on one level-1 return case), a step 2 item at
level 2 where a permitted walk and a validated hop are compared in one
search.

### Review and fastest-time target, 2026-09-16

The owner confirms a sub-15-second human run from rest at the home stand
to enemy pickup using tap jumps and no Force. The roughly 19-second bot
run is not a completed fastest-time search. Exploration samples a narrow
set of states and policies near a walking path; its stopping condition is
corridor exhaustion or a round limit, not optimality. Abstract 14-second
graph costs are not executed runs. The investigation and next experiments
are in [the search review](../../movement/records/2026-09-16-search-review.md).

Review fixes are implemented: exploration waits
for the declared fixture and resets its queue, selected-candidate telemetry
keeps the matching walk comparison, the graph cache supports its declared
capacity and parses bounded lines, every strafe edge checks its measured
entry velocity, and gap/replayed-hop rollouts observe the post-landing walk
long enough to observe the retained falling-endpoint failure. Fresh build
`move-build-review-20260916-a` passes the unseeded floor 36/36 with zero
parity mismatches and rewrite ownership throughout. With the 94,711-edge
exploration seed, level 1 passes 12/12 with zero mismatches, but two cases
still use legacy movement and the mean is slower than the retained greedy
arm; this does not pass the full state-planner gate. The stationary outbound
cases take 19.150 and 19.925 s, failing the owner's under-15-second target.
An offline zero-heuristic comparison finds lower modeled costs than A*,
but these are still approximate graph paths, not executed runs. The next
action is to diagnose predicted versus executed chain segments and fix
the seed's remaining ownership/performance failures before expanding the
search or proceeding to level 2. All evidence, including the initial
off-configuration failed runs, is retained in the search review.

### Replay review corrected; successor experiments open, 2026-09-17

Read [the correction record](../../movement/records/2026-09-17-replay-correction.md) and
[HANDOFF.md](../../movement/HANDOFF.md) before the retained
[original replay record](../../movement/records/2026-09-17-replay.md). Analyzer-v1 used the
late launch-stage confirmation as the flight start; its flight/ground split
and exact gap attribution are superseded by analyzer-v2 evidence.

The heading experiment did not reject the stored-policy hypothesis: large-
seed blue has two B-only admissions and two censored B results, large-seed
red has zero B-only and one censored B, and small-seed red has one B-only.
The measured entry/landing deviations are evidence of failed composition,
not proof of one cause. Full reproducible roots were not persisted by the
selected-field root telemetry.

The next actions are ordered: inspect all rescues; independently judge
censored B continuations without affecting A; test the smallest supported
validated successor policy; then prospectively capture all merge entrants
and losers with immutable identities and full same-root context. Compare
merge/no-merge on the same captured corpus and native cases. Old seed files
cannot recover discarded records, and exit spread alone cannot establish
causation. State-key loss and mismatched replay entry remain alternatives.

The unchanged default's reported 36-case floor and 12-case seeded arm pass
safety/parity, but `red_ret_L1_stand` still includes legacy ownership and the
seeded arm is 1,719 ms slower than the greedy reference. No sub-15-second
stationary no-Force run is demonstrated. `bot_moveChainAdmit` and
`bot_moveLandReplan` remain default off; neither established a default
improvement in the retained trials. Use matched repetitions and report
spread rather than treating a fixed 2-second threshold as a statistical rule.
Do not widen entry/reach gates or proceed to level 2 or held jumps.

### Rescues audited, censoring resolved, heading replacement refuted, 2026-09-17

Read [the rescue audit](../../movement/records/2026-09-17-rescue-audit.md) and
[the arm-B reference record](../../movement/records/2026-09-17-arm-b-reference.md) after the
correction record. Both are analysis and instrumentation; neither promotes a
default and neither claims a speed result.

Every retained B-only admission and every censored pair was audited. Two of
the three B-only admissions are real second-hop heading rescues (6.9 and 7.9
degrees at hop 1); the third is a 0.32-degree pair split by the onward test.
All five censored results were missing measurements: arm B was judged by the
control arm's walk horizon, so a B candidate that finished later than every
admitted control candidate could not be judged at all.

Arm B now has its own walk reference, rolled after the control's into its own
buffers, and the judge names `no_onward` instead of reporting a second landing
a one-hop plan never had. Per-candidate `end_stage`, `onward_max`,
`brake_frames` and `held_frames` are recorded, a launch event carries its own
`launch_level` command boundary, and `tools/movement_replay.py` is analyzer
version 3 with an `arm_isolation` check. Censoring is zero on all three routes
and isolation is clean over 1,092 judged candidates.

With every pair decided, the arms are eight B-only rescues against seven
A-only losses over 138 pairs on three routes: **replacing the aimed heading
with the stored heading is refuted.** The supported change is to offer both
constructions through the existing admission rules and keep the one ranking by
margin, which is implemented opt-in as `bot_moveHeadingOffer`, default off and
not qualified.

Every retained `no_second_landing` outcome was a one-hop candidate, so none of
them named a real failure. The measured cause at those roots is neither a
brake nor a hold: the post-landing walk covers about 56 of the required 128
units in the 48-frame window. A turn-related onward-test bias remains a hypothesis; displacement alone
does not establish the path travelled or the cause of the shortfall. It was **not** changed;
that is its own experiment, after the offer campaign.

A route-level diagnostics-off against diagnostics-on identity check is not
available on this harness: the fixture start is an rcon round trip at
wall-clock time, so two runs of one case do not share roots. Four matched
repetitions per arm share a modal 19,775 ms with 1,175 and 1,850 ms of spread.
Isolation, ordering and zero parity mismatches are what stand instead.

The sub-15-second target, the `red_ret_L1_stand` legacy ownership and the
seeded arm's deficit against the greedy reference are all unchanged.

### Heading offer measured; gate still unmet, 2026-09-17

`bot_moveHeadingOffer` (default off) offers the stored-heading construction as
an ordinary proposal beside the control construction and keeps the one ranking
by margin. Matched campaigns on one build: 36/36 default floor with `move`-only
ownership, 12/12 seeded in both arms, three named regression cases passing in
both arms, and three stand repetitions per arm. The offer arm is 508 ms per
case faster than its own control and narrows the deficit against the greedy
reference from 1,748 to 1,240 ms, but **still fails the level-1 gate** and
shows no benefit on blue. No sub-15-second run exists. The cvar is not
defaulted and not installed. See
[the heading offer record](../../movement/records/2026-09-17-heading-offer.md).

The remaining deficit against greedy sits in the respawn families, not the
stand family. The onward test is the other measured open item: 121 `no_onward`
refusals in one 12-case arm, with the mechanism recorded rather than guessed.

### Reroute admission: executable, not yet payable, 2026-09-18

`bot_moveReroute` (default off) replaces the selected-path refusal with a
validated alternative route from the candidate's landing, carried into
execution. The candidates the human landing probes isolated are now committed
and flown: 9.425 s remaining pickup at the jump-6 root against 12.500 and
11.775 s for the control, with no death and no parity mismatch, and the
default floor unchanged at 36/36.

It **fails the level-1 gate**: the seeded arm is 3,650 ms slower in total and
`blue_out_L1_stand` about 2,175 ms slower over three repetitions, with the
route-based ranking bit worse still. The measured cost is planned strafe edges
going stale after a route is installed mid-run (1-2 per blue stand run in the
control, 10-14 with the pass on), each one a replan and a retry.

Four arms were measured. Route-based ranking is the worst; admission alone is
slower than the control on the standard families; `bot_moveStaleAvoid` gives
the best stand medians and one 181-second stall; restoring the planned-chain
preference gives the best red stand median measured (17,600 against 20,700)
and then kills the bot on `red_ret_L1_stand`.

**Two pre-existing defects block this work and are reachable in default
builds.** A landed plan whose walk cannot cover `MOVE_ONWARD_DISTANCE` never
clears its onward stage and the replan gate waits on it for ever - two stalls
of about 175 seconds at one pocket, and a two-second hold in the slowest
passing run. And the planner's arrival model disagrees with the executor
often enough to produce runs of stale planned edges. `bot_moveOnwardBound`
and `bot_moveStaleAvoid` (both default off) bound the two symptoms; neither
is a repair, and the second can strand the bot.

The owner redirected the stand work on 2026-09-18: a human route is adapted
into an approved asset offline and executed at runtime
([route assets](../../movement/records/2026-09-18-route-assets.md)), rather than grown from
the online search. **The prefix from the stand into a route is landed**
([the route prefix](../../movement/records/2026-09-19-route-prefix.md)): all three routes
reach the enemy flag from `red_out_L1_stand`'s coordinates in 14,875, 15,350
and 19,000 ms including the walk, against the search's 21,150 ms median there
over 60 retained floors.
`bot_routePrefix` is default off, the comparison spans two physics settings,
and the run is open loop after the hand-off.

**Selection is measured too** ([where a route is worth taking](../../movement/records/2026-09-19-route-selection.md)):
the route wins at the stand and loses by 6.8 and 17.1 seconds at the two red
outbound respawns, because its start is behind them, and a suffix entry
cannot rescue that - the state at their nearest point on the line is 727-794
units/sec, above the walk's 250 units/sec ceiling. A route recorded from a
standing start has only its start demonstrated as an entry here. These walking
failures do not rule out every moving join, and new recordings are optional
evidence, not a dependency for covering cases. Independent search is the current
priority. The follower remains unqualified; its correction law is not a required
solution when a route entry is unavailable. The list below records the older
experiment agenda and is superseded by the current milestones above.

Earlier experiment agenda (repaired controls required): measure path-free admission at the break together with the
exit-speed term. The term is landed and safe but alone does nothing, because
66 of the 81 breaks had no admitted immediate candidate for it to promote
([the exit-speed term](../../movement/records/2026-09-18-exit-speed-term.md)); admission
alone was measured slower and stale-edged; together is untested and is the
only configuration in which either has something to work with. Then understand the deaths the tap pass produces (three across
two campaigns, one on `red_ret_L1_stand`); make a landing never a dead end
(the walk, or the arrival model that puts the bot there); then re-measure
admission with the chain preference; keep the jump-6 probe as the positive
control and `red_ret_L1_stand` as the safety case. See
[the reroute record](../../movement/records/2026-09-18-reroute-admission.md) and
[the tap measurement record](../../movement/records/2026-09-18-tap-onward-measurement.md),
which shows that admitting the continuation is not by itself what separates
the bot from the human runs.

### The leg budget: the target is priced, 2026-09-20

Read-only accounting of the retained confirmation worlds, no native run
([record](../../movement/records/2026-09-20-leg-budget.md),
[tool](../../../tools/movement_leg_budget.py), reports in
`.codex-research/leg-budget-20260920-a/`).

The route is a fixed **7,161 units**, 2.2 percent spread across eight worlds,
so leg time is distance over mean speed and nothing else. **15,000 ms needs a
477 units/sec mean.** Best observed is 442.1 (16,250 ms), worst 370.6
(19,275 ms), and the bot reaches 582.9 in flight. The target is therefore not
excluded by the physics and is not met by any measured leg.

The leg is **84.7 percent airborne**, so air-time fraction is not the problem.
The 15.3 percent on the ground is: **54.2 percent of grounded frames are the
search admitting nothing** and **24.4 percent are the committed plan's own
24-unit run-up**, 78.6 percent between them, and both are decisions rather
than physics. The remaining 11.7 percent is one friction frame per tap taken
on the landing frame, at 28 units/sec each, which is what a healthy landing
costs. Fast and slow worlds separate on exactly those two owners: 39-40
waiting and 13 run-up frames at 16,250 ms against 89-97 and 34-39 at
19,025-19,275 ms. Successor aim's gain is 90 fewer grounded frames, which is
the independent check on the metric.

**The open item this promotes:** during the waiting frames 1,029 candidates
were judged and **none admitted**, 748 of them refused on unsupported bit 256,
unclassified trigger contact. At a worked 582.9 units/sec landing every
`r0=0` candidate refused that way with no landing recorded, every admitted
candidate carried `r0=24`, and the commit walked 333 units/sec off before
launching. Bit 256 is an item, the flag, or the hurt volume after a fall, and
the forecast does not distinguish them. **Classify the contacts; do not add an
admission exception.** A secondary hypothesis, from the formula rather than
from measurement: `d0 = (0.695 * v0 + 76) * aim` with
`straightAims[] = {.85, 1.0, 1.15}` and `v0 = max(wishspeed, live speed)`
means the nearest available aim point at a 545 units/sec landing is 387 units
away, so the vocabulary's reach grows with the speed it most needs to keep.

Bounding the prize: holding entry speed through every waiting and run-up frame
and recovering the same distance gives 16,250 → 15,690 ms on the best world
and 19,275 → 17,717 on the worst. That is a **lower** bound - it removes the
loss during those frames and gives no credit for the re-acceleration a
repaired landing would not need - so first-order removal alone does not reach
the target and the rest must come from the flights that follow.

### The trigger refusals classified: correct, 2026-09-20

One diagnostic world, existing `bot_forecastTriggerLog`, no source change
([record](../../movement/records/2026-09-20-trigger-classification.md), evidence in
`.codex-research/trigger-class-20260920-a/`). The run reproduces retained world
`confirm-1-r8` byte for byte, same 761-sample trace hash, so the in-rollout
printing did not perturb it.

All 547 contacts are classified. Through the productive middle of the leg they
are the map's single `trigger_hurt` volume: pooled over the expensive grounded
runs, hurt 221 against medpak 33, blueflag 47 (all in the final approach) and
push 1. **The bot brakes at those landings because every continuation that
keeps the speed kills it.** The 24-unit run-up is a deliberate brake, not a
bookkeeping defect, and no admission exception is warranted.

Retired by this: classifying the contacts; `bot_forecastItems` as a general
remedy (33 of 288 in-leg contacts, two sites); and shorter first-hop aim
fractions, because `d0` is normalised into a heading by `AimAt` and the hop's
reach is set by the physics, so all tap-now candidates at a high-speed landing
fly the same distance into the same pit.

Opened by this: **price a route by what it costs to fly, not to walk** - but
see [the route pricing record](../../movement/records/2026-09-20-route-pricing.md), which
narrows this sharply. The live planner already prices by flight; only the
reroute gate does not. Also open, and small:
the +12,825 landing, where the bot arrives at 624.3 units/sec - the fastest
state recorded in this work - and medpak contacts outnumber hurt contacts
before it brakes to 253.7. One site, one pilot, not a general change.

Lowered by this: the in-search-work coupling. 547 prints from inside the
rollout reproduced a retained trace exactly, and the `va` mechanism is already
excluded, so the 2,000 ms incident looks specific to that patch.

### Route flight-cost pricing: refuted, 2026-09-20

[Record](../../movement/records/2026-09-20-route-pricing.md). Source **reverted**; the tree is
hash-verified back to the reference manifest and reproduces `confirm-1-r7` byte
for byte against the frozen build.

Two corrections first. All **46** in-leg replans came from `Graph_PlanState`,
never `Graph_Path`, and the state planner already prices links from the speed
actually carried - so the claim that the planner prices routes at walk speed is
withdrawn. And the reach-versus-straight rule for tap-now refusals is
contradicted by the data: 55 % admitted where the reach overruns the straight
against 28 % where it fits.

What is real is narrower: the **reroute gate** discards the state planner's own
flight-priced `costMs` and re-prices the same cells with `PathRemainingMs`.
`RerouteInstall` logs both - `cost_ms=10142` against `walk_ms=25335`, about 31
seconds of route claimed on a 16,450 ms leg.

Substituting the flight price on both sides of that gate is **worse**. Off:
16,450 / 16,450 / 17,200. On: 16,375 / 16,375 / 19,700. Admitted reroutes fall
from 13 to 0-1, and removing the 250 ms margin entirely does not bring them
back (three identical runs, 19 `slower_route`, 0 `rerouted`).

The mechanism is the useful part: the candidate's first stretch is charged its
**measured** flight while the incumbent is charged the planner's **expectation**
for the same ground. The walk model was a pessimism that offset that asymmetry.
It cannot be removed until both sides are the same kind of number, and the gate
cannot obtain a measured flight for the incumbent, which is never rolled out.

### Search rate: refuted, the waiting frames are availability, 2026-09-20

[Record](../../movement/records/2026-09-20-search-rate.md). Source **reverted**; 1,117 of
1,117 manifest sources match the reference build.

`StraightEligible` gates a non-landing grounded frame by both `MOVE_GAP_RETRY`
(250 ms) and `MOVE_GAP_CADENCE` (200 ms), so the floor is 250 ms - the modal
gap measured inside the waiting runs. `bot_moveSearchRate` replaced both with
one value. The treatment is visible in the logs (40 gaps of exactly 25 ms at
rate 0, 8 of exactly 100 at rate 100) and the control reproduces
`confirm-1-r7` byte for byte.

Three runs an arm, alternating: control 16,275 / 16,275 / 19,000; rate 0
20,525 / 18,525 / 17,975; rate 100 19,250 / 19,150 / 16,275. **No arm
improves**, at either rate. Waiting frames barely move (59.0 control against
52.0 and 58.7) while run-up frames rise in both arms.

The mechanism was already in the evidence: during the waiting frames the search
had judged 1,029 candidates and admitted none, because the refusals are flights
into the hurt volume. More looking cannot create an option physics refuses.
**`waiting_for_a_committable_plan` is availability, not latency** - a proposal
against those frames must change the state the bot arrives in, not how often it
looks once it is there.

Next, and cheap because it needs no build: `bot_moveExitSpeed` is 2000 and
`ExitCredit` adds exit speed straight into the score, so the ranking rewards
exactly the landing speed these refusals then reject. Sweep it and read the
result off the leg-budget owner split rather than off leg time, which has
defeated three pilots in a row at this sample size.

### The exit-speed credit is priced too high, 2026-09-20

[Record](../../movement/records/2026-09-20-exit-speed-sweep.md). **No source change, no
rebuild**: `bot_moveExitSpeed` is a cvar on the frozen reference build.

`ExitCredit` is `landSpeed * (bot_moveExitSpeed * .001)` and the ranking is
`score = margin + ExitCredit`, so at 2000 the credit is twice the landing speed
and dominates the term it supplements. The three preceding records all point at
the decision before the landing; this is the cheapest test of it.

Eight fresh counterbalanced pairs, 500 against 2000: mean **18,693.8 ->
17,718.8 ms**, median **19,025 -> 17,512.5**, best 16,275 -> 15,750, five pairs
improving and three regressing, mean paired delta -975 ms. **The predicted
mechanism moves with it**: waiting frames 77.9 -> 60.9, run-up frames 32.2 ->
25.9, mean speed 387.7 -> 408.7 units/sec, search cost unchanged at 38-39 ms
median. The 15,750-15,775 trajectory appears five times across the test arms and
never in any control arm this session.

**The tail is worse and unexplained**: worst 20,025 -> 21,025, with pair 8
regressing 4,750 ms. Screening also shows the relationship is not monotonic -
`bot_moveExitSpeed=0` is worse than both - so the credit is mispriced, not
wrong.

Floors at 500: **36/36, zero deaths, zero parity mismatches, movement owner.**
Four blue cases first returned `corner start refused` with zero samples and
zero elapsed - fixture setup, not movement, at `--parallel 4` - and all four
pass on a serial re-run; both sets of evidence are retained.

Open: the value 500 is not established (0 worse, 2000 worse, three points
sampled); a finer sweep judged on the owner split, and an explanation for the
tail, come before adopting it. Parity still makes zero comparisons. **Both open
items are closed by the two steps below.**

### The fixture's start level decides the leg, 2026-09-20

[Record](../../movement/records/2026-09-20-start-level-determines-the-leg.md). **No source
change, no rebuild**: 100 native worlds on the frozen reference build.

`movement_bench.py` sends `bot_corner_start` when its polling loop gets there,
and records the resulting `corner_level` as bookkeeping. Conditioned on that
number, `red_out_L1_stand` is deterministic: **25 ms worst-case spread over 15
(start level, arm) cells and 87 worlds**, mean 16.7 ms, against 3,525-5,250 ms
unconditioned within an arm. The start level fixes the first touchdown after
the stand in 99 of 100 worlds, and the same mapping holds on the 16 retained
confirmation worlds of the previous step.

The start level is set before the runner moves, so it is pre-treatment and an
arm cannot cause its own. The first touchdown is not: at start level 3,400 the
credit itself changes it, which is why the stratum must be the start level.

This is why leg time defeated three pilots: at three runs an arm, an arm's
times are a draw over start levels whose means differ by up to 5,000 ms.

Levels are `corner_level`, which `result.json` reports and `--corner-level`
takes; the leg budget's first touchdown is one frame later.

Closed the same day by the step below. Still open: why a level maps to a given
first flight, and whether the mapping survives another build, map, case or
level.

### The finer exit-speed sweep: 500 confirmed, tail explained, 2026-09-20

[Record](../../movement/records/2026-09-20-exit-speed-finer-sweep.md). **No source change, no
rebuild**: five arms - 250, 500, 750, 1000, 2000 - alternated round-robin over
20 sequential rounds, 20 worlds an arm, the incumbent re-measured in session.

Stratified on start level and weighted by how often each stratum occurred:
**500 at 16,976 ms against 2000 at 17,942**, with 250 at 17,128, 750 at 17,735
and 1000 at 17,740. The 966 ms win reproduces the paired design's -975 ms to
within 9 ms, from a different design in a different session.

The optimum is real but shallow and not the same at every start: at the
43-world stratum 250 and 500 tie and everything from 750 up loses 1,500 ms; at
the 34-world stratum 500, 750 and 1000 tie and both 250 and 2000 lose 505 ms.
500 is the best compromise over the observed starts, not the best value at
every start.

**The tail is explained.** Pair 8's 4,750 ms was two worlds drawing different
start levels; about 3,800 ms of it is that. The remaining 975 ms is a genuine
cost of 500 at start level 3,300, reproduced to within a frame in this session
(21,000 against the earlier 21,025), and its mechanism is a 27-frame wait at a
landing 55 units/sec faster that the geometry admits no continuation from.
Refuted on the way: landing speed does not predict what a landing costs - the
median cost is one frame in every band above 350 units/sec and the most
expensive band is 470-510, not the fastest.

Still a configuration value, not code, and **not installed**. Floors at 500
stand from the previous step; none were run for 250, 750 or 1000 and none is
recommended. Parity still makes zero comparisons.

### The start level is pinned in the harness, 2026-09-20

[Record](../../movement/records/2026-09-20-pinned-start-level.md). **Tool change only**:
`movement_bench.py` and `movement_bench_lib.py`; no native source change and
no rebuild, and the runner re-verified all 1,117 manifest sources on every run.

`--corner-level MS` holds `bot_corner_start` for a chosen level, verifies the
level the world reports, and retains a world that misses under `<case>.miss-N`
while a fresh one replaces it (`--corner-level-attempts`, default 4). Unset,
which is the default and what acceptance floors use, behaviour and retained
layout are unchanged.

Calibration: the start lands on the level of the last play-log row the runner
has seen, three worlds of three, so the default lead is zero. Reachable:
`corner_level` 3,350 landed in 6 worlds of 6 and returned **19,050 ms every
time**; 3,325 landed in 3 of 4 and returned **16,450 every time**, after the
post-add poll was shortened from 500 ms to 50. The same configuration unpinned
spreads 4,300 ms.

Two strata are pinnable and no more, because the runner's clock is a play log
flushed in roughly 200 ms bursts and it cannot wait for a frame it never sees.
A server-scheduled corner start would make this exact rather than statistical;
that is a source change and nothing measured needs it yet. Ten new tests; the
movement Python suite is 112 and passes.

Open: compare arms at one pin and report which pin, because an arm's advantage
is not the same at every start.

## Phase 4: second map

Widened by the owner on 2026-09-27 to wide map compliance: roadmap step 1
above names the map set and the navmesh question.

- Run preparation on a second known map with no code change, once with its
  navmesh present and once with the mesh removed.
- Gate: the same case shape passes there at levels 1 and 2 in both runs.
  The guaranteed coverage is the walk, corner, hop and strafe graph from the
  BSP; the mesh may add off-mesh edges and change times, never pass or fail
  on that guaranteed set.

## Phase 5: retire legacy owners

- Only after phase 4 and shared-lifecycle qualification: retire the superseded
  owners and legacy search. Keep prepared plans and their evidence as supported
  inputs; retiring an old controller does not prohibit recorded plans. Archive
  removed artifacts under the workspace policy. Permanent deletion requires
  explicit approval. The qualified movement system becomes the only path.
- Gate: benchmark unchanged on both maps, source count reduced, README
  updated.

## Out of scope until phase 4 passes

Defended play, body contact, combat, aiming, and map-specific fixes. Their
evidence in the archive is retained for when they return. The 2026-09-27
roadmap brings situation assessment, Force choices and position behaviour in
through end-to-end milestones after wide map compliance; a minimal combat
layer for chase and base is an open owner decision there.

### Independent references and offer telemetry, 2026-09-17

Historical continuation work: [human landing probes](../../movement/records/2026-09-17-human-landing-probes.md)
isolate selected-path rejection of physically viable immediate jumps. The
next stand-run correction is a validated alternative onward path carried
through forecast and execution; heading expansion failed at the measured
root. No new production policy is qualified and the sub-15 target is unmet.

The offered B candidate now retains its own reference horizon. Its eligibility
no longer changes A's reference. Mode-aware telemetry distinguishes proposals
from observe-only candidates. See [the correction and fresh qualification](../../movement/records/2026-09-17-independent-reference.md).
The turn explanation remains unproven; onward travel and objective progress
are now measured. No admission threshold was loosened.

Verification at that date: 36/36 default floor, both 12-case seeded safety arms,
and eight blue trials per arm pass survival/parity. The offer remains
unqualified and default off; its blue median is worse. A controlled pad-cost
probe improves red outbound respawn5 by 2,675 ms at the median over three
runs per arm, establishing route selection as a measured next issue. The
artificial penalty is not a production change. Correct realizable pad-route
costs before the next qualification; the stationary sub-15 target is unmet.
