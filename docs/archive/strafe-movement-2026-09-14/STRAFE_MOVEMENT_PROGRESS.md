# Strafe movement progress

Updated 2026-09-14. This scorecard owns measured status; use the
[handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) for next actions and
[project contract](STRAFE_MOVEMENT_PROJECT_PLAN.md) for gates. Older checkpoints
and their evidence map are in [progress history](STRAFE_MOVEMENT_PROGRESS_HISTORY_2026-09-12.md).

**Owner priority:** return to defender-free walking, idle and momentum continuity.
Body-contact/defender planning is deferred to v2. The following trials remain
evidence, not the immediate work queue; see the handoff for the solo baseline.

**Latest September 14 dynamic recovery trials:** native command/braking
validation plus failed-leg memory is implemented as an opt-in prototype. Final C
survives 16/16 candidate worlds and captures in 14; all eight body worlds capture,
but two confinement episodes return (7.250/7.500 s). Red fast pursuit fails to
capture through the 90-second window. The earlier aggressive scan/progress trial
caused two native falls and is not active. Dynamic recovery is not qualified;
`bot_recoveryNative` stays off for functional reasons, with CPU handled separately.
Four same-build default-off controls also include a later fatal fall; all
36 outcomes and eight failed setups are retained. Deferred v2 work is native-validated
candidate connections with an explicit body-contact contract, not another
timer/direction adjustment. Nothing installed.
[Trials, failures and implementation contract](STRAFE_MOVEMENT_DYNAMIC_RECOVERY_2026-09-14.md).

**Previous September 14 ground descent correction:** the shared corridor evaluator
now admits checked small descents over a floor lip. Both retained post-body
stalls no longer occur in eight extended worlds: 8/8 capture alive, zero audited
confinement, six expected body guards and no unexpected execution/world mismatch.
Twenty native worlds survive in total. Persistent pursuit remains mixed, with
nine worlds confined and a repeated slower-pursuer control regression. First-60s
body-cohort idle falls 38.000→15.750 s while walking rises 163.000→186.500 s;
combined idle/walking does not improve. Dynamic obstruction planning remains
next; no activation, installation or full qualification.
[Correction, controls and retained failures](STRAFE_MOVEMENT_GROUND_DESCENT_2026-09-14.md).

**Previous September 14 body validation:** native pre-launch/airborne body guards
are exercised; clear-placement cohort captures alive 8/8 with six correct body
refusals and no unexpected execution/world mismatch. Two distant stalls remain
(5.500/6.750 s), so survival/capture does not close recovery quality. Fast pursuit
confines control and fallback alike. Earlier route falls remain retained; a
native fixture hull check now refuses invalid initial placements. CPU cost is
separate optimization work under owner direction, not a functional acceptance
veto. No installation or full qualification.
[Cases, failures and next target](STRAFE_MOVEMENT_HOP_BODY_2026-09-14.md).

**Latest September 14 bounded hop supplement:** repaired legacy command cadence,
unlocked steering, native settings and coherent contact-state output. The
repaired twelve-case legacy cohort has one observed death versus six before;
163/167 logged landings match XY at printed precision, but four mismatches and
the separate C11 ground fall remain open. Runner controls remain alive 12/12.
The default-off route supplement is tested in four short and four extended
pairs: all 16 final-build worlds are alive, with 12/12 admitted maneuvers rejoining
and no audited distant confinement. Four complete post-capture trips average
35.056/32.675 s (control/fallback), dominated by one phase-sensitive case.
Fixed-window walking and idle worsen slightly (66.050/68.550 and 5.500/5.750 s);
one initial trip is slower. No broad walking/idle win, body/cost qualification,
ordinary activation or installation is claimed.
[Implementation, exact cases and open defects](STRAFE_MOVEMENT_HOP_FALLBACK_2026-09-14.md).

**Latest September 14 legacy comparison (diagnostic):** twelve paired residual
starts and four paired repeats collected (32 worlds). Legacy requests grounded
jumps at 25 ms in all ten walking starts and 175 ms in both idle starts.
Four near-home first-cohort returns improve 1.600–2.150 s, but six of twelve
legacy continuations have fatal falls; two of four legacy repeats also fail.
Runner has no deaths in these sixteen worlds. One low-Force timing win reverses
on repeat. This isolates locomotion with reconstructed physical starts, not
full Bot2 combat/power policy or historical private-state replay. No qualification,
merge or installation. [Cases, limitations and next direction](STRAFE_MOVEMENT_LEGACY_COMPARISON_2026-09-14.md).

**Latest September 14 sequence planning:** direct executor telemetry and shared
lookahead for native Speed purchases are implemented. In the unchanged eight
cases, all capture alive with zero audited confinement. Normal-spawn walking
falls 86.250 to 46.825 s (45.7%), with combined idle unchanged. All-case walking
falls 157.300 to 115.225 s; idle rises 9.750 to 10.250 s. Four extended candidate
worlds also finish alive with zero confinement. Complete post-purchase trips
average 37.706 to 35.425 s against four extended controls; walking totals
62.100 to 32.300 s, while idle rises 3.500 to 3.750 s. These are measured pairs,
not exact private-state replays. Low-resource movement and broader qualification
remain open. The attempted straight-chord planner made no admitted shortcut
and was removed from active execution. No installation.
[Implementation, direct decisions and full-trip evidence](STRAFE_MOVEMENT_SEQUENCE_PLANNING_2026-09-14.md).

**Previous September 14 walking inventory (analysis only):** all eight current
recordings contain 130 observed CSV walking episodes totaling 172.775 s;
155.500 s falls within the declared scoring windows and reconciles exactly with
existing audits. Supported control owns 91.0% of walking; 69.8% occurs at Force
50 or higher. Logged geometry repeatedly associates walking with short hop runs
and non-hopping intervals/corners, but missing per-frame gates prevent exact
causal replay. Every episode, evidence reference, unresolved cause and separate
carrier supplement is in the [inventory](STRAFE_MOVEMENT_WALKING_INVENTORY_2026-09-14.md).
No new native run or performance improvement is claimed by this analysis.

**Latest September 14 off-line approach implementation:** the missing bounded walking connection
is implemented and measured in the unchanged eight-case cohort. All eight
capture alive; all eight avoid audited prolonged confinement (preceding C: 7/8).
Seven native approaches occur; six have two-second observed continuation of
404–485 units without supported release, and one is end-censored. The affected
blue fixture's idle share falls 9.13→2.28%. Ordinary walking and first-trip
results remain mixed, with startup/observation phase limits; full movement
acceptance remains open. No installation.
[Implementation and evidence](STRAFE_MOVEMENT_HANDOFF_APPROACH_2026-09-14.md).

**Previous September 14 handoff redesign:** executor-owned snapshots, actual-state
admission, frontier yield and actual-resource route entry are implemented.
Baseline plus three candidates complete 32 worlds alive. Final C removes the
reproduced route-entry resource parking; seven of eight worlds have no audited
confinement, but one blue post-capture localnav episode lasts 5.750 s. Ordinary
idle/walking remain mixed, and differing first-observation/start phases prevent
claiming end-to-end gains from shorter observed elapsed times. The movement
gate fails; no qualification or installation. Next: bounded native entry from
outside the prepared corridor, retaining the unchanged cohort and gates.
[Contract, full results and evidence](STRAFE_MOVEMENT_HANDOFF_CONTRACT_2026-09-14.md).

**Previous September 14 prefix publication:** eight diagnostic worlds distinguish
the stop before first publication from an early stopped vertex behind the bot
after publication. A checked straight run-up trial improves one matched fixture
trip 35.425→32.125 s and leaves the other at 33.275 s. Normal-speed walking
worsens on both teams, so `bot_supportedPrefixRunup` stays off. All twenty
diagnostic/comparison worlds capture alive, health floor 90; current 1105-source
and staged bindings verify for twelve comparison worlds. No broad acceptance
or installation. Next: exact straightness contracts and actual-state entry at
the early stopped vertex. [Record](STRAFE_MOVEMENT_PREFIX_PUBLICATION_2026-09-14.md).

**September 13 baseline movement work:** dropped-flag retrieval is
deferred by the owner to later objective/combat work. Two ordinary-route
preparation-fallback trials complete sixteen native worlds alive, health floor
90. The broad variant worsens normal-spawn walking; the restricted coast-expiry
variant slows both matched fixture trips by 50 ms and has mixed normal-spawn
results. Neither is accepted; `bot_supportedPrepareFallback` remains off.
Both builds have source/settings/staged verification at collection; the current
restricted build binds 1105 sources and eight worlds. Focus next on temporary
published-prefix braking after the ordinary jumppad landing.
[Trial record](STRAFE_MOVEMENT_PREPARATION_FALLBACK_2026-09-13.md).

**September 13 dropped-flag diagnosis (now deferred):** native probes establish clear
flat floor at the recorded flag and four surrounding points, above Kejim's hurt
volume. The later stop is on a walkable slope with lower support at the refused
XY; safe complete descent is still unproved. This is ordinary ramp investigation,
not required pit retrieval. The owner excludes paths through `trigger_hurt`.
All 14 declared probes and their failures are retained; native source/module
and artifact verification passes. [Geometry record](STRAFE_MOVEMENT_DROPPED_GEOMETRY_2026-09-13.md).

**September 13 return attribution:** 18 same-build worlds compare the
two largest historical slow cases with experiments off, prior return fixes,
and the combined corner policy. All capture alive, health floor 90; 1105 native
sources and all staged bindings verify. Both historical slow trajectories
reproduce with experiments off, with all 708 recorded momentum samples equal.
Observed outcomes group by startup phase on the 8 ms native movement clock;
these comparisons do not justify a controller change. Historical matrix scores
and qualification gates remain unchanged. Next is the dropped-objective descent
refusal. [Attribution record](STRAFE_MOVEMENT_RETURN_COUNTEREXAMPLES_2026-09-13.md).

**September 13 corner continuation:** the added base-goal idle is traced
to a stop just short of an intermediate mesh corner. A smaller stopping radius
fails. A bounded continuation toward the next path point, governed by existing
native ground/hull/hazard checks, improves both complete trips 34.175→32.150 s
and sampled idle 2.000→0.250 s. Controlled body obstruction is refused; both
blocked arms capture in 35.525 s. Both new dropped-flag pairs improve, although
one walking total worsens. No installation; experimental defaults remain off.
[Current record](STRAFE_MOVEMENT_CORNER_CONTINUATION_2026-09-13.md).

The clean restarted integration matrix captures 60/60 alive, health floor 90,
passes 26/26 body compatibility checks and completes 30/30 resource cases without
deaths. Current sources/settings/staged binaries verify for all 116 worlds and
the sixteen affected worlds. First-return mean delta is -0.0017 s against retained
guard controls, effectively neutral: 27 slower pairs, 16 worse idle fractions,
30 worse walking fractions; T5 remains 0/8. This is not full qualification.
Defended discovery has no direct continuation event; the controlled native body
case is stationary and moving cache interception is covered by compiled checks.
An interrupted high-port campaign is retained as setup failure, not scored as
movement outcomes. The later dropped-objective idle has actual missing-floor
refusals and requires a separate admissible descent/route investigation.


**September 13 owner execution:** live full-game body-guard continuation is now
measured: the normal-speed guarded case captures alive in 12.225 s, with three
clear controls at 10.150–10.300 s and no false guard. Four accelerated defended
worlds complete 600 s with zero confinement. A natural enemy guard event ends in
a pistol death on the platform; it is retained, not counted as successful travel.
Retained guard-campaign scoring gives mean first-return delta +0.0433 s,
28/60 slower pairs, 5 worse idle fractions and 27 worse walking fractions; T5
remains 0/8 in every repetition. Initial walking experiments reduce the problem
interval but regress complete trips. Acquisition investigation found that
fallback takes the first hop without Speed during a 500 ms floor-contact refusal;
the hop itself was not absent. See
[owner execution and next blocker](STRAFE_MOVEMENT_LIVE_HANDOFF_2026-09-13.md).
No installation or full qualification is claimed.

**Return-acquisition follow-up:** checked step-height join geometry and optional
authored-tap timing, combined with partial spans, improve base-goal full trips
from 37.025 s to 34.150/35.125 s and dropped-flag trips from 45.000 s to 43.100 s
in both repetitions. All eight capture alive, health floor 90. Isolated ablations
remain mixed. Four 180 s defended worlds finish: captures 4/3 red/blue versus
2/3 controls, no candidate confinement or falling deaths; red walking rises
30.1→33.2% and blue idle rises 4.0→4.8%. This is not broad qualification or a
causal paired combat result. Experimental switches remain off. Current build
verifies 1105 sources/two binaries and sixteen completed staged worlds; broader
integration collection is recorded in the
[acquisition record](STRAFE_MOVEMENT_RETURN_ACQUISITION_2026-09-13.md).

That first combination subsequently completes 60/60 solo captures alive, health
floor 90, 26/26 body checks and 30/30 resource completions. First-return mean delta
is +0.0792 s against retained guard controls, with 32 slower, 25 worse idle and
39 worse walking pairs; T5 remains 0/8. Source/settings/staged bindings verify.
**Latest revision** preserves timing inside validated tap chains, removing the
same-build R2-blue 0.925 s regression. Twelve affected worlds capture alive;
current 1105-source/two-binary/staged bindings verify. Base-goal trips improve
37.050→34.000 s in both pairs, but idle rises 0.500–0.750 s. Dropped-flag pairs
improve 1.900 s / regress 1.575 s: the faster bot travels farther before the
native flag-return timer changes the objective. The return itself is essentially
equal in the slower pair. Broad checks from the previous build do not qualify
this revision; experimental defaults and full-trip acceptance remain unchanged.

Earlier normal-speed follow-up: four 180 s defended worlds complete, 4/3 captures for
red/blue in both partial-spans arms, no confinement. Blue walking worsens from
32.9% to 36.5%; idle rises on both teams. Each blue world retains a falling death
after combat-associated displacement under ordinary movement, with no flight
guard event. These failures remain open. That experimental build verifies
1105 native source hashes and both binaries; neither walking switch is promoted.

**Earlier September 13 implementer update:** the flight contact mechanism is reproduced
with retained cached/native state. The original opponent demo reports grounding
on runner 0 at ascent loss; the native body-proxy replay records an underside
contact and the same recovery failure. A short moving-body takeoff guard refuses
on world ground; clear/moving-away/higher-clearance controls retain 64/64 exact
position/velocity pairs. Review confirmed the completed integration subset:
60/60 solo captures, all alive, health floor 90, zero churn; 26/26 body cases and
30/30 resource completions. That build's 1104-source/two-binary bindings matched
at review. No solo world in that set triggered the new guard. The owner execution
above supersedes the previous pending continuation/scoring status; an accepted
outbound walking repair remains open. See the
[contact record](STRAFE_MOVEMENT_FLIGHT_CONTACT_2026-09-13.md). No installation or
T5/T6/K07 completion is claimed.

## Current position

**Latest: background landing recovery implemented; broad movement remains open.**
All 18 eligible landing releases in the 60-world behavior arm query again after
25 ms while fallback retains movement during preparation. The first trial's
premature prefix braking is superseded. All 60 worlds captured, with no deaths,
falls or churn. Mean case capture-time delta is -0.013 s against takeover:
effectively neutral. Twenty time pairs, eight idle-fraction pairs and 23
walking-fraction pairs regress; T5 remains 0/8. Affected checks pass, including
26/26 body cases and 30/30 resource completions. The final diagnostic-only reset
passes six affected native cases with correct release accounting; current source
and binary bindings match. That verification is separate from the frozen
60-world behavior arm.
[Current record](STRAFE_MOVEMENT_BACKGROUND_RECOVERY_2026-09-13.md).

Two normal-speed defended discovery worlds on the preceding trial produced
13/9 captures and no confinement, but blue plunged and died by `MOD_FALLING`.
The flight adapter controlled the launch; a nearby overhead enemy makes body
contact plausible, not proved. These worlds had no landing-requery events and
do not qualify the final build. Supported outbound walking without Speed is the
largest observed walking category, including long intervals at full Force.
[Defended evidence](STRAFE_MOVEMENT_LANDING_REQUERY_2026-09-13.md#defended-discovery).
No installation or T5/T6/K07 completion is claimed.

**Previous: independent crossing speed and bounded join approach implemented and
measured, not installed.** R5 red mean improves 3.633 s and blue 2.408 s against
speed-guard v1; all six scored joins admit at 70 Force after an 800 ms approach,
with no below-50 sample during the approach. 60/60 captures, no deaths/falls,
health floor 90, zero churn; 26/26 body checks and 30/30 resource completions.
All-source/binary bindings match. One native speed refusal re-acquired after 1 s;
combined height/contact speed refusals are covered by compiled checks only.

Whole-travel qualification remains open: the previously fast R5-blue repetition
is 0.150 s slower, nineteen of sixty pairs have worse time, six worse idle
fraction and nineteen worse walking fraction. T5 stays 0/8. Full-world stationary
and zero-input totals rise; no broad idle/recovery improvement is claimed.
[Prior record](STRAFE_MOVEMENT_JOIN_APPROACH_2026-09-13.md).

The older build/start-phase exclusions below are historical interpretations,
not established causal conclusions. The measured resource/radius mechanism does
not rule out upstream build or startup effects. Continue from the current handoff.

### Prior checkpoints

**Released plans that nothing had found fault with are repaired and measured, not
installed.** Of the corner-span arm's 11/13/10 contract releases per twenty
worlds, 6/6/6 remain, and localnav frames fall from 72/132/74 to 38/36/31 with
zero-input-while-stationary frames from 0/24/0 to none. Three families were
attributed: a runner still airborne judged against a ground contract â€” which is
what the one `turn_envelope` release was, at 425.362 u/s against a 425.0 limit
that tolerates 40 more, not the overrun the corner-span record read it as â€” a
vertex the runner overflew and landed 65-75 units under, and a landing 26.0-70.6
units off the line. The first two are repaired by deferring a ground clause in
flight, resuming on the interval that holds the runner, and judging a vertex's
limit against the braking room left to reach it; the third is retained and
attributed. Across 60 worlds: 60/60 captured, no deaths, falls or lower-floor
departures, health floor 90, churn 0, bevel and ramp neighbourhoods unchanged with
no sample below 150 u/s. Thirteen of twenty cases are faster on the mean; R5-blue
is 2.5 s slower on a start-phase outcome reproduced on both builds. Regressions
pass: 26/26 body cases and 30/30 resource completions without any contract
release.
[Repair and evidence](STRAFE_MOVEMENT_DEGRADE_RELEASE_2026-09-12.md).

**The spanned-bevel corner is repaired and measured, not installed.** The
corridor's mid-map bend arrives as two vertices 8.4 units apart, and the corner
compiler judged the first of them while preparation was still streaming: its
bevel span stopped at the prepared frontier, the 8.4-unit leg became the exit
leg, `trim` collapsed to 3.36 and the pass speed to 26.5, and nothing
reconsidered the vertex once the rest of the path arrived. The runner braked from
425 to 45-49 u/s there in 46 of the ramp arm's 60 worlds. In that neighbourhood
the twelve exact-12 first returns now hold 237.3-319.4 u/s with no sample below
150, against 45.6-49.5 u/s and 0.425-0.450 s below 150 before. All twelve
exact-12 returns are faster, by 0.60-1.40 s, and 15 of the twenty declared cases
are faster on the three-repetition mean, -9.325 s in total. Across 60 worlds:
60/60 captured, no deaths, falls or lower-floor departures, health floor 90,
churn 0. Regressions pass: 26/26 body cases and 30/30 resource completions
without refusal.
[Repair and evidence](STRAFE_MOVEMENT_CORNER_SPAN_2026-09-12.md).

Two costs were retained and attributed there, and the release repair above has
since resolved both kinds: localnav frames were 72/132/74 against 60/63/62, with
one world -- R6b-blue in r2 -- 41 of the 132. That world's release was an airborne
runner against a ground contract, not the speed overrun the corner-span record
names; the rest was the seeded outbound descent, never on a measured return.

**T5's resource state is measured; its infeasibility is not.** The exact-12
cases seed the runner with 12 Force and one 13.275-second Speed window; Force
regenerates only after Speed expires and reaches 25-40 by capture, against the 50
a level-3 Speed costs, so the last 4.5-5.5 seconds run at the 250 u/s ground cap
by declaration. That is a measured constraint on the tail. It is not a lower
bound on the return: no phase-by-phase budget at achievable speeds has been
computed, so whether 16 s is reachable with the remaining time is open. This
paragraph read "T5 is not reachable on these cases by movement alone, and this
cycle measured why" until 2026-09-12; limited Force prevents a second Speed cast,
which is not the same claim. T5 remains 0/8 per repetition, failed; returns are
17.075-22.35 s against 16. K07, T2/T4 and defended T6 remain open. Installed
baseline remains `reviewready-b` / `525f1b50`.

The latest cycle repaired a release bypass review found in the preceding one and
measured no change on this declaration, because the state it guards does not occur
in these sixty worlds. A case that produces it is an open experiment, not a
finished one.

R5-blue's 3.8-second loss is attributed and the previous start-phase reading is
withdrawn: the fast-return join misses when the runner crosses the 128-unit scan
radius around route 4's sample 50 before its Force reaches the 70 that sample's
launch costs. Distance at that moment separates 14 joins from 4 misses across six
builds, 18 of 18 worlds. Reanalysis only, no collection.
[Join race](STRAFE_MOVEMENT_JOIN_RACE_2026-09-13.md).

| Target / evidence scope | Result |
|---|---|
| Crossing speed guard (latest, 2026-09-12) | **reviewed release bypass repaired; no measured change**: a crossing that was past, wide *and* over its swept speed reported `lateral` and was left behind with the speed unjudged, because the gate read the reported clause instead of the state. The guard fires in **none of the sixty worlds** and no release in either arm names `speed`, so both binaries decide this declaration identically: 60/60 captures, releases **6/6/6** in both arms and all twelve the same retained `lateral` family at index 3, T5 0/8, churn 0, health floor 90, no deaths, falls or corridor drops. +0.041 s on the twenty-case mean, 8 of 20 faster, every case within Â±0.27 s -- run-to-run variation, not an effect. Body and resource regressions identical to the baseline. Also corrects the T5 "movement alone" and 0.13 s hop-ceiling conclusions and the corner-span source binding. [Record](STRAFE_MOVEMENT_SPEED_GUARD_2026-09-12.md) |
| Degraded release of published plans (preceding build, 2026-09-12) | **two of three release families resolved**: contract releases 11/13/10 to **6/6/6**, all six the same retained lateral miss; `localnav` 72/132/74 to **38/36/31**; zero-input while stationary 0/24/0 to **none**; stationary 34/117/42 to 52/48/43; zero-input 106/156/101 to 149/158/119, of which 102â€“136 are above 250 u/s under the plan rather than localnav. 60/60 captures; no deaths, falls, corridor drops or low-jump/unpaired resource releases. 13 of 20 cases faster on the mean, +0.058 s overall and âˆ’0.070 s without R5-blue's start-phase 2.5 s. Bevel 237.2â€“319.7 and ramp 237.2â€“241.4 u/s, none below 150. T5 remains 0/8. [Record](STRAFE_MOVEMENT_DEGRADE_RELEASE_2026-09-12.md) |
| Spanned-bevel corner (preceding build, 2026-09-12) | **one stopped arrival per team resolved**: exact-12 bevel minima 45.6â€“49.5 to 237.3â€“319.4 u/s, 0.425â€“0.450 s below 150 to none. 60/60 captures; no deaths, falls, corridor drops or branch abandonment. All twelve exact-12 returns faster by 0.60â€“1.40 s; 15 of 20 cases faster on the mean, âˆ’9.325 s total. `localnav` 60â€“63 to **72/132/74**, 41 of the 132 one world; stationary 41/53/44 to 34/117/42; zero-input 124/134/128 to 106/156/101. Seam 250.0â€“255.9 to 244.6â€“426.1, ramp unchanged. T5 remains 0/8. [Record](STRAFE_MOVEMENT_CORNER_SPAN_2026-09-12.md) |
| Ramp-entry continuity (earlier build, 2026-09-12) | **two stopped arrivals per ramp resolved**: exact-12 ramp minima 45.2â€“50.6 to 237.2â€“241.5 u/s, 0.300â€“0.650 s below 150 to none. 60/60 captures; no deaths, falls, corridor drops or branch abandonment. Ten of twelve exact-12 returns faster by 0.15â€“0.55 s. `localnav` **4â€“6 to 60â€“63**, all on the seeded outbound descent; stationary 52/66/65 to 41/53/44; zero-input 138/119/123 to 124/134/128. T5 remains 0/8. [Record](STRAFE_MOVEMENT_RAMP_ENTRY_2026-09-12.md) |
| Checked seam continuation (preceding build, 2026-09-12) | **local stop resolved**: red R2/R7f12 seam minima 45.3â€“49.8 to 250.0â€“250.4 u/s in all six first returns. 60/60 captures; no deaths, corridor drops or branch abandonment. `localnav` 6/4/4, stationary 52/66/65, zero-input 138/119/123. T5 remains 0/8; overall travel results mixed. [Record](STRAFE_MOVEMENT_SEAM_REPAIR_2026-09-12.md) |
| Corridor/rejoin repair (earlier build, 2026-09-12) | **observed failure resolved**: 60/60 captured; 0 deaths, lower-floor departures or branch abandonment. Completed rejoins 39/39/37; 7/3/3 entries remain open at world end. `localnav` **775/765/735 to 4/3/5**, stationary **436/437/419 to 58/48/49**, zero-input **656/627/619 to 118/82/102**. Minimum z **1368.125 to 1808.001â€“1808.004**. T5 still **0/8** each repetition. [Record](STRAFE_MOVEMENT_CORRIDOR_REPAIR_2026-09-12.md) |
| Tap admission (historical tapfix3, work order step 2) | **met and measured**: flight-resource refusals 6/6/6 to 1/2/1; none of the 30â€“32 gradable resource refusals per repetition lands on a low jump. Every finished branch instance has a terminal record. Regressions 26/26 and 30/30. Corridor minimum improves from 7.9â€“12.6 to 227â€“231 u/s but exposes the lower-floor drop now repaired. [Record](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md) |
| T1 resource-reason churn (step-4 / corrected reanalysis) | **met**: 0/0/0 in control and candidate |
| T1-all all-reason churn (step-4 / corrected reanalysis) | **met**: 29/25/34 to 0/0/0, outside the control's declared band |
| T2 original landing turn (step-4 / corrected reanalysis) | **failed** on its 350 u/s minimum: 2 of 37 control turns, 0 of 28 candidate. Recovery half met |
| T2-branch windows (step-4 / corrected reanalysis) | **partial / unqualified**: entry 32/33 in r2; descent 50/52, 53/55, 50/52; worst corridor samples 7.9â€“12.6 u/s; no scored rejoin. Later successful native branch exits do not establish every T2 speed window |
| T3 refusal continuation (step-4 / corrected reanalysis) | **improved, not met**: 42/41/42 of 57/59/56 against 7/5/8 of 45/41/50 |
| T4 accepted curves and passes (step-4 / corrected reanalysis) | **not advanced**: a third as many reached, pass rate 52â€“57% to 44â€“49% |
| T5 travel time (step-4 / corrected reanalysis) | **failed in every arm including control**: 0 of 8 per repetition; remains 0/8 on the latest span build. The exact-12 cases seed 12 Force and one 13.275 s Speed window and regenerate to 25â€“40 by capture against the 50 a level-3 Speed costs, so their last 4.5â€“5.5 s run at the 250 u/s ground cap by declaration [attribution](STRAFE_MOVEMENT_CORNER_SPAN_2026-09-12.md#what-the-retained-traces-attribute-the-ground-travel-to) |
| T6 safety and lifecycle (step-4 / corrected reanalysis) | Solo half **met**: 0 falls/deaths, 180/180 captured, no sample below z1808. Defended: both teams capture at normal speed, no normal-speed confinement; four chasm plunges, three at normal speed. Two knocked and two self-steered under `supported`; normal-speed split one knocked and two self-steered. [Attribution](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md) |

The [corrected measurement contracts](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md)
supersede four frozen step-4 figures: corridor window 81/93, 89/98, 84/96 rather
than 84/92/87; T3 counts hold but 38 of 41 already meet their threshold at refusal,
with median following-second minimum speed instead separating 117â€“127 control
from 361 candidate; T5's eight denominators are four exact-12, two zero-Force,
and two Force-1â€“11, with post-expiry mean speed failing four episodes in every
arm/repetition; branch-frame comparisons remain blocked while censoring and
missing terminal records are not distinguished. Reconciled step-4 branch frames
are 11923â€“12166 against 16649â€“17810 lane frames.

No overall speed or return-time qualification is claimed. Latest reductions are
measured recovery and low-speed costs. Later repairs do not inherit the complete
step-4 campaign. The T6 falling-death clause remains unchanged and its owner
decision does not block independent movement fixes.

## Evidence map

| Question | Record |
|---|---|
| Why the fast-return join misses, and what it costs | [Join race](STRAFE_MOVEMENT_JOIN_RACE_2026-09-13.md) |
| Latest repair, build and the evidence corrections | [Speed guard](STRAFE_MOVEMENT_SPEED_GUARD_2026-09-12.md) |
| Preceding release/degrade repair and build | [Degraded release](STRAFE_MOVEMENT_DEGRADE_RELEASE_2026-09-12.md) |
| T5 attribution and ranked implementation | [T5 phases](STRAFE_MOVEMENT_T5_PHASES_2026-09-12.md) |
| Spanned-bevel corner repair and build | [Corner span](STRAFE_MOVEMENT_CORNER_SPAN_2026-09-12.md) |
| Preceding ramp-entry repair and build | [Ramp entry](STRAFE_MOVEMENT_RAMP_ENTRY_2026-09-12.md) |
| Preceding seam repair and build | [Seam repair](STRAFE_MOVEMENT_SEAM_REPAIR_2026-09-12.md) |
| Preceding corridor/rejoin repair | [Corridor repair](STRAFE_MOVEMENT_CORRIDOR_REPAIR_2026-09-12.md) |
| Earlier tap repair and exposed corridor drop | [Tap repair](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md) |
| Corrected metrics | [Measurement contracts](STRAFE_MOVEMENT_MEASUREMENT_CONTRACTS_2026-09-11.md) |
| Defended departures | [Fall attribution](STRAFE_MOVEMENT_FALL_ATTRIBUTION_2026-09-11.md) |
| Repeated branch campaign | [Step 4](STRAFE_MOVEMENT_PACKAGE_D_STEP4_2026-09-11.md) |
| Branch coverage/authoring | [BR-A](STRAFE_MOVEMENT_PACKAGE_D_BRA_2026-09-11.md), [BR-B](STRAFE_MOVEMENT_PACKAGE_D_BRB_2026-09-11.md) |
| Older benchmark/cost/lifecycle evidence | [Historical evidence map](STRAFE_MOVEMENT_PROGRESS_HISTORY_2026-09-12.md#evidence-map) |

Replace current status when results change. Keep full experiment narratives in
dated records rather than appending superseded next actions here.
