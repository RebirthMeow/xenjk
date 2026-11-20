# First installed-candidate human defense review

The resulting [pickup/pad repair](STRAFE_MOVEMENT_PICKUP_PAD_2026-09-10.md)
is implemented and natively tested. The findings below describe the original
failed installation, not the current candidate.

Installed source checkpoint `509454ee`. Owner played blue against the red
Runner_v2 at normal speed, approximately 23:10–23:14 local September 9.
Saved evidence: `.codex-research/strafe-human-play-20260910-031010/`:
`final.csv`, `final-console.log`, `final-games.log`, `review.json`,
`pickup-review.json`, and `vertical-bounce-episode.csv`. The game log also
contains earlier matches; scope its events to the last match before counting.
The original human and runner demos remain under the OpenJK homepath's
`base/demos/autorecord/2026/09/09/mpctf_kejim 2026-09-09_23-09-49/`.

The recording contains 952 bot samples over 233.175 game seconds, eight flag
pickups, zero captures and eleven sampled deaths. Normal confinement audit
finds no episode; that does not establish good movement or recovery.

## Owner's primary finding: pickup destroys useful return movement

The owner observed fast arrivals, a stop after flag pickup, then ground running
instead of jumping. Seven of eight pickups report `stand_entry_resource`.
Six last pre-pickup samples exceed 1000 u/s; most such arrivals have 26 Force
remaining. Seven pickups have slow zero-wish samples within the next three
seconds; the remaining pickup is followed by death within 250 ms. The full-force
pickup also pauses (`stand_entry_brake`) then has a sampled jump at +1.25 s.
Two low-resource pickups have their first sampled jump at +6.25 and +15 s;
several others have no jump input captured before death. Four-Hz sampling can
miss single-frame jump pulses, so do not treat absent input samples as proof
of no native jump. Combat damage confounds later portions of these trips.

`ai_bot2_route.c:StandEntry` explains the cached-route resource decision: its
stand-to-stand guide requires 50 Force and enough remaining active Speed, or
100 Force without Speed. It repositions/brakes at the start and can deactivate
Speed when parked to regenerate. The release label persists into supported
fallback; it does not mean StandEntry owns every later paused frame.

**Highest-priority correction:** make pickup-to-return movement useful with
the resources actually remaining. Inspect StandEntry versus supported-plan
ownership and preparation together. An unavailable cached guide must not keep
pulling the runner back to its entry or waiting for that guide's resource state
when a safe ordinary return is available. Prepare a return continuation before
pickup where practical; preserve native objective publication and current-state
validation. Braking for a real turn is legitimate; stationary planning/recharge
at the contested stand is the behavior to eliminate. Do not lower a cached
guide's validated resource requirements or force high-speed turns blindly.

Use a bounded native pickup/return case with the observed low Force and active
Speed, both teams, and a full-resource control. Measure pickup-to-clearance,
zero-wish time, actual hop/landing events and useful return progress; retain
resource/lifecycle checks affected by the fix. Then run one ordinary defended
playtest. No full qualification sweep or combat redesign is needed for diagnosis.

## Secondary finding: vertical recovery trap

At level 197550–207550, XY remains approximately (2467.921,3970.417) while Z
cycles between 1376 and 2011. Local navigation issues zero movement and the
bot dies from falling damage at 207625. This is ten seconds of no horizontal
progress that the 3D confinement detector misses. The owner identified a
jumppad; newer early-return navigation owners bypassed its legacy steering.
Preserve as a secondary concrete counterexample,
after the repeated pickup defect. Another pit death at 228900 follows a large
damage/displacement event and is not yet classified as avoidable.

Melee produces three swing-episode prints in one close encounter. This match
does not establish reliable combat escape. Prioritize the owner's repeated
pickup/return complaint over inferring a broad combat deficit from capture/death
counts.
