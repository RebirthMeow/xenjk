# Human Kejim trajectory study, September 10

## Scope and evidence

The owner requested studying the human demo corpus before another corner fix.
This is a bounded snapshot-trajectory study, not video review, input imitation,
or native qualification. No movement source or installed game was changed.

The existing inventory contains 4,426 files / 4,409 unique hashes; its Kejim
subset contains 1,088 files / 1,085 unique hashes. Four selected demo hashes
were checked against that inventory and decoded to EOF:

| Sample | Recording | Snapshots | Gravity |
|---|---|---:|---:|
| 00 | community demo | 36,586 | 760 |
| 01 | community demo | 36,711 | 800 |
| 02 | community demo | 37,526 | 800 |
| 03 | tournament demo | 24,047 | 800 |

Samples 01–03 also contain zero-gravity initialization states; these are not
used as movement examples. Total: 134,870 snapshots. Across this small selected
sample there are 22 observed pickup episodes: four capture-counter increments,
17 deaths and one loss/end transition. One capture is a 0.1-second pickup near
home, not a full return. Do not use pooled capture duration as a route metric.

Evidence root: `../.codex-research/human-corners-20260910-a/`.
`summary.json` binds original absolute filenames, hashes, decode status and
settings. `sample-*.jsonl` retains full snapshots including PVS entities.
Compact derivatives omit entities for trajectory analysis; partial `ps-01`
and `ps-02` conversions are retained, superseded by `compact-*` derivatives.
`analyze.py`, `plot.py`, and their JSON/PNG outputs retain the analysis.
The decoder is the existing external `<demo folder>/JkDemoExtract` package, through
`tools/strafe_demo_audit.py`. It supplies no raw usercmds and does not apply
server-command configstring updates. Do not infer exact buttons or treat
snapshot spacing as input cadence. No observation is approved for control
fitting; PVS does not establish absence of collisions or external impulses.

## Concrete corner observations

![Corner trajectories](../.codex-research/human-corners-20260910-a/corner-comparison.png)

Bot comparison uses client 1 only in
`fastreturn-playtest-20260910-d/listen-{red,blue}/base/bot2-play/20260910-002056-1575-0.csv`.
Times below are raw server/level milliseconds, not match-clock timestamps.
Windows were selected for descriptive examples, not an unbiased performance
estimate. The human examples here are non-carriers with gravity 760; bot
examples are carriers with gravity 800. Routes, resources and opposition
are not matched. The blue example traverses the area in the opposite direction.

| Area | Human sample 00 | Bot | Interpretation |
|---|---|---|---|
| Upper bend near (1619,2573,1808) | 1186094–1187117: velocity turns 59.6 degrees, speed 608.5 → minimum 581.8 → 632.8 u/s; grounded 12.5% of samples | Red 41000–42000: 425 → 56 → 455 u/s | Human follows an offset curved airborne line and briefly lands; bot follows a sharper stop-and-turn line. |
| Blue ramp near (241,-362,1824) | 1108709–1109732: velocity turns 101.7 degrees, speed 456.5 → minimum 435.2 → 524.4 u/s; grounded 34.4% | Blue 29500–30500: 411 → 34 → 358 u/s | Continuous turning exists in this area; this is not proof the reverse bot line can use identical inputs. |

The padded player-state screens report no damage-event change, active
knockback timer or special hand state in these two human windows. Possible
passive over-max health decay is retained as an ambiguity. This is not a
clean-contact verdict. Z was checked: lower-floor trajectories at similar XY
are not used as evidence for an upper-floor corner.

There is also a bot-only longer stall: red 44500–45500 remains at
(1225.014,1824.030,1808.125) with supported ownership, zero speed and zero
movement input. Recovery starts at 45750 and arrives at 46525. An enemy is
about 1352 units away at the start; the record does not establish the cause.
This must be separated from a brief deliberate corner brake.

## Carrier example and limitations

![Carrier return trajectories](../.codex-research/human-corners-20260910-a/carrier-returns.png)

Sample 02 carries from 823853 to 839627: a 15.774-second capture,
confirmed by the player-state capture counter and matching sidecar capture
event. Pickup speed is 425.9 u/s, Force is 35, median horizontal speed is
456.6 u/s, and 17.1% of samples are grounded. Speed is active at pickup and
later expires while travel continues. Force rises during the trip, so this is
not a controlled test of a monotonically depleting 35-Force budget.

The sample's other three pickups end in death after 5.577, 3.036 and 9.933 seconds.
All three start with zero Force. Retain these outcomes: one successful return
does not establish that the route or low-resource policy is generally safe.
One zero-Force pickup window, 388946–389969, changes velocity heading 57.3
degrees while maintaining at least 404 u/s, mostly airborne; the carrier dies
later. Moving efficiently and surviving are separate observations.

Jax sample 03 has a full 11.650-second capture, 52111850–52123500, starting
with 50 Force and 169.5 u/s. Median speed is 697.7 u/s; 40.2% of samples
are grounded. The early pickup-exit window 52112400–52113400 turns velocity
99.2 degrees while increasing speed from 310.3 to 661.6 u/s, with no lower
intermediate speed and 38.1% ground samples. Its padded player-state screen
has no damage/knockback/hand-state reasons, with the same unresolved contact
and passive-health-decay caveats. This is carrier evidence at gravity 800.
Jax also has 13 carrier deaths, a 63.4-second capture, the near-home 0.1-second
capture, and one final loss/end transition; the fast example is explicitly
selected, not representative of every attempt.

The full speed traces also show real slowdowns and stops. Jax briefly brakes
just after pickup before the measured accelerating turn, and slows/stops near
home before the capture. Sample 02 has a sharp mid-return slowdown. Thus the
lesson is not "humans never stop" or "always jump": preserve momentum through
feasible travel turns and allow localized braking when necessary. The selected
turn window must not be confused with the whole return's behavior.

## Recommended next implementation

The strongest immediate evidence concerns execution policy, not navmesh
resolution. `bot2_path.c` requires ordinary non-crossing corners to be within
24 XY units, within 32 Z units, grounded and below 80 u/s before advancing.
`ai_bot2_supported.c` disables corner baking for streaming returns. Those
rules directly explain a repeatable brake-to-near-stop pattern. This study
does not prove that the navmesh geometry is adequate everywhere.

1. Inspect why the existing checked corner rounding is disabled for streaming
   returns. Add bounded preparation of the next turn without rewinding the
   moving path or making preparation own a stationary wait.
2. Use a swept, floor/clearance-checked curve and a feasible exit velocity for
   traversable corners. Advance by crossing a validated turn exit rather than
   arrival below 80 u/s. Preserve the strict arrival rule for corners without
   a validated moving alternative; do not globally remove the threshold.
3. For the 80/20 delivery, get continuous ground turns working first and
   preserve already-valid hops. Add airborne turn execution only where its
   sweep, landing and momentum checks pass. Human lines are shape references,
   not executable command recordings.
4. Separately trace the zero-input supported stall at (1225,1824,1808). Bound
   no-progress ownership and rejoin a valid moving segment promptly; do not
   assume all hesitation is the same corner condition.
5. Verify the two concrete corners and the stall location in both directions,
   with low Force and Speed expiry. Measure minimum speed, zero-input time,
   progress, collision/fall outcomes and return time. Use a fresh native build
   for qualification, then a short defended normal MP smoke and human retest.
   Do not reopen the full historical qualification chain first.

Keep K07 open. No controller improvement, navmesh fix, survival gain or
production qualification is claimed by this observational study.
