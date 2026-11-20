# Opposition idle and walking fixes

Follow-up to the retained [rate campaign](STRAFE_MOVEMENT_OPPOSITION_RATE_2026-09-06.md).
The owner requests fixing the observed idle, walking and stall behavior.

All three worst stalls target dropped enemy flags at pit-floor z24, following
Runner deaths. The native flag remains active until its normal return timer.
The movement controllers reject the unreachable endpoint, including every
otherwise safe corner-recovery candidate. Flags return at level320775
(red5),534475(blue5),70200(red2.5), after which movement resumes. This is
commanded braking at an impossible destination, not simply wall collision.

First isolated change: project each dropped flag onto navigation geometry,
requiring a point within64 units. Shared per-team checks run at most every
500 game ms. An unreachable enemy drop selects stand staging; an unreachable
home drop does not displace the home-return goal. Native flags, their timers,
resources and physics are untouched. Recheck so a falling flag that lands on
usable ground becomes eligible again. This is generic endpoint validation,
not a map-height rule or a guarantee that every complete route is traversable.

Declared first comparison: fresh `strafe-build-drop-reachability-20260906`,
both runner teams against CHASE/BASE at5x for600 game seconds, normal maps,
distinct ports29420/29421 and private homepaths. Preserve samples and demos;
compare the same idle/walking/confinement audit. Random matches are unpaired.

Walking investigation also found that one late unsupported path sample
discards every previously validated segment. A separate subsequent experiment
will retain a completed safe prefix and replan at its end, keeping all trace
budgets, floor/envelope checks and corner braking. No unvalidated points may
enter that prefix. Assess walking and stalls before deciding to install it.

Second candidate additionally observes the same bounded corner-recovery
watchdog before each movement adapter: repeated supported-path ownership
must not postpone its1.5-second progress trigger. Existing geometry/state
checks and global recovery query/trace budget remain. Validate three exact
pit-drop reproductions plus one reachable dropped-flag control for60 game
seconds each. The optional fixture uses native pickup and LaunchItem once,
declares pose/resources/drop coordinates, then retains ordinary gravity and
the native30-second flag return. No later fixture intervention is permitted.
Then repeat both teams at5x/600 seconds and1x/120 seconds, plus normal MP
at5x and an isolated eight-bot cost check. Retain failures and decide whether
the second candidate improves walking as well as stalls before installing.

The exact fixtures exposed an insufficient first check: pit floors themselves
have navigation polygons. The self-projection check accepted all three pit
drops, reproducing26.5–28.5-second idle spans. Those failed fixtures are kept
as `strafe-drop-case-{red-low,blue-high,red-high}-20260906`, with a reachable
control that successfully picked up. The revision checks a connected path
from the runner and the actual destination's damaging trigger/liquid volumes.
Checks are cached per client/flag team for500ms and globally limited to one
path query and one64-entity hazard neighborhood per game tick. This is still
not a proof of complete controller traversability across every off-mesh edge.

The connected/hazard-aware candidate (`strafe-build-stall-fix-final-20260906`)
removes distant confinement in all three exact pit tests. Native return
remains30 seconds; pickups occur30.05 seconds after setup. Reachable control
pickup occurs3.0 seconds after setup. A stricter >=64-unit displacement
within3 seconds check fails red-low (4.4 seconds), while other cases move
within1.175 seconds. Keep this failure distinct from fixing the impossible
goal. Audit: `strafe-drop-audit-20260906T012346622005Z.json`.

Its two ten-minute defended5x matches show red9.8% idle/28.4% walking,
blue12.4% idle/38.1% walking (including1.4% flagstand wait), and distant
confinement0/5.75 seconds. Two120-second1x controls have no>=5-second distant
confinement, idle7.1/12.8%, walking28.4/21.8%. Walking is mixed, so these
results do not establish a walking improvement. They are retained in
`strafe-stallfix-opposition-audit-20260906.json`.

Next candidate (`strafe-build-stall-fix-hop-prefix-20260906`) accepts partial
paths only if a completed, hop-safe horizontal segment exceeds320 units,
matching the hop controller's minimum distance. Walking-only prefixes yield
to cached routes/fallback. Corner scanning adds96-unit candidates while
retaining64/32-unit candidates and all floor/body/hazard checks. It evaluates
24 candidates over separate ticks, not more work in an individual tick.

That candidate's red-low fixture exposed a radius mismatch: execution checks
include the validated point's vertical floor-clearance offset in a3D distance,
whereas candidates are generated with a horizontal radius. It repeatedly
selected then rejected the same96-unit step. Retained failing audit:
`strafe-hop-prefix-drop-audit-20260906.json`. The correction bounds horizontal
distance with a0.125-unit numerical margin; independent floor-height checks
still limit every step. Rebuild: `strafe-build-stall-fix-qualified-20260906`.
The build name is an identifier; qualification claims depend on results below.

All four exact cases pass on that corrected build. The previously slow
red-low escape now moves at least 64 units in 2.75 game seconds; blue-high
takes 1.15 seconds, red-high 0.925 seconds, and the reachable control 0.9
seconds. The pit flags are never selected, remain under their native 30-second
return deadline, and are picked up from the stand at 30.05 seconds. The
reachable dropped flag is selected and picked up at 3.0 seconds. All four
runners remain alive. See `strafe-qualified-drop-audit-20260906.json`.

The next collection used both teams at 5x for 600 game seconds and at 1x
for 120 seconds, using the same chase/base opposition and sampled definitions
as the baseline. A separate normal MP check and isolated cost check follow.

That collection completed. The ten-minute 5x runs have no >=5-second distant
confinement on either team. Idle is 8.6/13.6% (blue includes 3.9% goal wait),
walking 38.8/31.2%. Normal-speed blue exposed a distinct 15-second carrier
stall: Runner at (28.331,-1562.550,1912.126), chase enemy at
(28.331,-1532.424,1912.125), only30.126 units north and pressing forward.
The exact solo start succeeds without recovery, ruling out a static corner
alone. Objective-relative diagonals miss the lateral corridor between enemy
and wall. Four cardinal64-unit candidates are now added before the existing
fan, preserving body collision checks and one candidate per frame. A static
native collision-body fixture reproduces the geometry separately from the
real-opponent recording. Walking results remain mixed; do not claim general
walking improvement from these unpaired matches.

## Installed build and final results

Installed `strafe-build-stall-fix-lateral-20260906`. Both installed game DLLs
match the measured build's SHA256. The MP executable is unchanged.

Final defended runs use one Runner_v2 against CHASE and BASE Bot2 opponents.
Percentages below are time weighted over observed alive game time. Stationary
means grounded speed below 20; walking means grounded speed 20 to below 300.
Distant confinement means remaining within a sampled 64-unit region for at
least five seconds while more than 96 units from the goal. All durations are
game seconds, not wall time; quit tails are excluded.

| Case | Idle | Walking | Longest stationary pause | Longest distant confinement |
|---|---:|---:|---:|---:|
| Red, 5x, 600 seconds | 9.0% | 24.9% | 3.75 s | 5.0 s |
| Blue, 5x, 600 seconds | 12.9% | 26.5% | 3.75 s | 8.5 s |
| Red, 1x, 120 seconds | 5.2% | 32.4% | 1.0 s | None >=5 s |
| Blue, 1x, 120 seconds | 5.9% | 38.2% | 1.0 s | None >=5 s |

The previous ten-minute 5x baseline had idle 13.1/16.0%, walking 31.8/33.3%,
and stationary pauses 17.5/22 seconds. Final red includes 1.1% legitimate
flagstand waiting; blue includes none. These unpaired matches support local
improvement, not a statistical or universal reliability claim. Normal-speed
walking remains mixed. Four-Hz confinement sampling does not detect every
larger loop. Audit: `.codex-research/strafe-lateral-opposition-audit-20260906.json`.

On the installed build, the red-low pit regression passes with 64 units of
displacement in 2.9 seconds and native flag return/pickup timing preserved.
The other two pit cases and reachable control passed on the preceding build;
they were not repeated after adding cardinal recovery candidates. The static
body-block fixture escapes laterally, with longest stationary pause 1.75
seconds and first 64-unit displacement 5.275 seconds after setup. It tests
collision geometry, not the enemy's active combat. Evidence:
`strafe-lateral-red-low-20260906/drop-audit.json`,
`strafe-qualified-drop-audit-20260906.json`, and
`strafe-lateral-corner-audit-20260906.json` under `.codex-research/`.

A separate normal MP listen run at 5x with both enemies completed cleanly:
3.9% idle, 37.4% walking, longest stationary pause 0.75 seconds, and no
five-second distant confinement. Both controllers used 25 ms ticks and the
requested roles. Audit: `strafe-lateral-listen-audit-20260906.json`.

Isolated 800-frame cost checks used the installed build. Eight bots cost
0.828 ms p95 / 1.149 ms maximum for movement and 1.321 / 1.894 ms for the full
server frame. All eight remained alive; 149 frames had all eight moving.
This meets the eight-bot numerical budget, but does not qualify M3 activity.
The 16-bot stress check cost 3.154 / 5.166 ms for movement and 4.819 / 8.236 ms
for full frames. All 16 remained alive, at most 14 moved simultaneously, and
all clients were dispatched with valid clocks. It does not meet the eight-bot
budget numbers or establish fully active 16-bot qualification. Reports:
`strafe-lateral-cost8-report-20260906.json` and
`strafe-lateral-cost16-report-20260906.json`. M3a remains open.

Installation archived the previous root/base DLLs and symbols, route catalog,
and runner personality assets in
`.codex-archive/20260906T0147388211476Z/`. Its `manifest.json` records each
original and archived absolute path. To restore, close the game, archive the
current destination files, then copy each archived file back to its manifest
original path. Retain the archive. No files were permanently deleted.
