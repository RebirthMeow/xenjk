# Supported live movement development

M3a development, 2026-09-05. This follows the conservative Kejim supported
probe. It does not replace the qualified M0–M3 controller or its gates.

Hypothesis: the existing hop command kernel can perform repeated native
pickup/return/capture without full-force waits when production movement owns
an independently checked navmesh path. Initial integration deliberately keeps
the probe's corner stops; corner optimization and planned-reference speed
qualification remain separate checks.

`bot_supportedHop=0` preserves the previous controller. Mode 1 originally
acquired from ground only below full force or with Speed active. The later
listen-server fix also permits full-force fallback after cached routes decline;
an existing supported path keeps priority. Mode 2 prefers supported traversal
even with full force. Both preserve active
cached-route ownership and grounded acquisition after an airborne handoff.
Candidate paths are checked incrementally with at most 32 preparation traces
and one path query across clients per server frame. Unsupported floor samples
reject the path; insufficient hop clearance selects walking. These sampled
checks remain experimental and are not a general safety certificate.

First frozen development set: both flagstand starts, phase 0, initial force 0,
Speed inactive, 120 seconds each, mode 2. Require two native captures per
trial, no deaths, no force spending/power activation, and no unexplained
10-second spatial stall. Preserve all failures. Compare first pickup times
to the retained 27.7–28.5-second conservative hopping probe. Travel speed is
not qualified by this comparison: matched-resource planned references remain
required. Subsequent variable-force/expiry and clock-phase cases must be
declared before collection.

The observer uses `bot_hopProbe=6` only for initial stand/resource setup and
recording. Each command then comes from `Bot2_MovementOnlyCommand`; goals,
pickup, return and capture are native. No resets between legs. Run using a
fresh unique `tools/strafe_fixture_build.py` build and
`tools/strafe_continuous_runner.py --hop-probe 6 --supported-hop 2
--initial-force 0 --speed-expiry 0 --phase 0`, with separate team/output/port
arguments and the existing Kejim assets/catalog arguments. Normal pacing;
at most four isolated native worlds. This is outcome evidence, not cost.

Moving-player lifecycle, dynamic obstruction handling, complete hop envelopes,
M0–M2 regression checks and continuous-workload cost remain qualification work.

The first two normal-pacing trials passed all declared gates: red pickups at
28.650/85.900 seconds and captures at 57.325/116.425; blue pickups at
28.500/86.775 and captures at 57.800/115.325. Both remained alive, spent no
force, submitted no power commands and had no confined 10-second stalls
(extent <128 units with unchanged objective/carry/capture state). Audit:
`.codex-research/strafe-supported-live-smoke-audit-20260905/summary.json`.
Build: `strafe-build-supported-d75a0182cb8c477586230d39e158e5b8`.

Next frozen set (before collection): both teams × phases 0/3/6 × initial
force/remaining-Speed pairs (0/0, 20/0, 50/0, 20/400, 20/8000), 30 trials.
Same two-minute/two-capture/no-death/no-spending/no-stall gates. The 400 ms
expiry exercises ground preparation; 8000 ms expiry is a travel case, whose
actual grounded/airborne state must be reported. Native Speed alone may be
active initially; require its one natural transition within a command frame
of the declared expiry. No later power commands. Freeze the watchdog fix
in a new build; first compare normal and accelerated phase-0 zero-force
traces on that build, then use acceleration only for outcome collection.
`tools/strafe_supported_batch.py` records the exact case inventory, commands
and results; `tools/strafe_supported_report.py` audits them.

Scope distinction: mode 6 disables the cached Speed catalog. These trials
exercise production objective dispatch and native Speed expiry while the
supported path owns movement. They do not measure the cached Speed-guide
expiry release into supported traversal. Initial force plus native regeneration
also does not simulate survival-power spending or external replenishment.

The watchdog revision's normal/accelerated phase-0 cases matched all 4,800
movement, command-axis, resource and objective samples per team after excluding
absolute clocks. Pacing evidence: `strafe-supported-pacing-v2-20260905`.
The full 30-case accelerated matrix then passed: 60 native captures, no deaths,
no force expenditure/power commands, no confined stalls, 144,000 samples.
All native clocks/settings, loaded module/build hashes and case conditions
passed the audit in
`.codex-research/strafe-supported-live-matrix-v2-audit-20260905/summary.json`.
Final controller build: `strafe-build-supported-8f4a05fab7414ae2bb623e817cf7847c`.

No-Speed first pickups span 28.400–29.625 seconds. With 8000 ms remaining
Speed, they span 25.350–31.675 seconds; Speed is not uniformly faster with
this conservative corner policy. All native transitions occur at 425 or
8025 ms, one command after the declared duration. Three travel-expiry cases
are airborne (red phase 3; blue phases 0/3), with horizontal speed changing
407.6→411.1, 425.6→417.6 and 424.0→418.1 units/s respectively. The other nine
expiry cases are grounded. Every case completes two captures.

The same final build, with supported traversal disabled, retains all 12 M0
pickups and continuous route ownership, with exact command/movement matches
to the preceding continuous-native-flags regression. Nominal 7.025 seconds,
worst 7.650. Evidence: `strafe-m0-supported-v2-20260905/summary.json`.
This is exact equality against that recent controller, not the older original
historical trace. Full M1/M2 reruns remain required for M3a qualification.

Next checks declared before collection: (a) two phase-0 stand starts with
initial force 100, catalog enabled, `bot_supportedHop=1`, no initial Speed,
120 seconds; require live cached-route acquisition followed by supported
travel, native capture, no deaths, and report waits/power commands honestly;
(b) the existing isolated eight-bot 20-second cost/impulse workload with
`bot_supportedHop=2` and movement-only mode, then sixteen-bot stress. Cost
includes complete movement dispatch and native server frames; it does not
include continuous objective lifecycles in this bounded fixture.

Catalog coexistence passed both phase-0 development cases: three native
captures per team, no deaths or unexplained confined stalls. Both acquire
cached routes and subsequently submit nonroute jump commands on returns.
Only generic command 4 (Speed) and 0 are observed. This is ordinary resource
spending/regeneration with no inter-leg resets; the no-spending gate from the
isolated supported matrix does not apply to this separately declared test.
Evidence: `strafe-supported-catalog-v2-audit-20260905/summary.json`.
Cached-guide expiry release itself remains unmeasured.

The isolated mixed-dispatch cost workload also completed. Eight bots: movement
p95 0.453 ms, max 0.597 ms; full server-frame p95 1.539 ms, max 2.219 ms.
All eight remain alive and dispatch every frame; 301 frames have all eight
moving, including 132 during the declared recovery interval. Sixteen-bot
stress: movement p95 0.939 ms, max 2.478 ms; full-frame max 5.520 ms, with one
later MOD_FALLING death (client 10). Retain this failure. Both runs have 800
movement frames, correct clocks, unchanged sources and exact build bindings.
Audits: `strafe-supported-cost8-v2-audit-20260905`,
`strafe-supported-cost16-v2-audit-20260905`, and
`strafe-supported-cost-provenance-v2-20260905` under `.codex-research/`.
The cost schema does not record supported-vs-cached ownership per client;
this is an enabled mixed workload, not eight guaranteed supported followers
or continuous-objective cost qualification.

Remaining gates: matched-resource planned references, corner performance,
cached-guide expiry handoff, complete path-envelope/deviation and dynamic
obstruction handling, then moving-player objective lifecycle and final M1/M2
regressions/M3 affordability in continuous play. The controller remains opt-in
for qualification. The owner subsequently authorized normal-game installation;
see `KEJIM_RUNNER_PLAY.md` for the installed build and listen-server fixes.

## Prepared support intervals — September 6

Named delivery blocker: the complete generated Kejim routes spend about eight
seconds per round trip walking on locally clear portions of edges whose hop
flag was disabled by a separate elevation/sweep change. Preserve native
sample-level eligibility as typed walking/hopping intervals. Use one shared,
caller-owned executor that passes collinear interval boundaries and retains
the existing stopped arrival at genuine turns and objectives. This is the
first supported portion of the prepared-route execution contract; gap-edge
composition and optimized corner connectors remain open.

The new mode is `bot_supportedSpans=1`; baseline is zero on the same build.
It keeps the existing full floor/ceiling/sweep checks, the global preparation
budget, ordinary hop kernel and 320-unit cutoff. Partial preparation uses the
existing prefix fallback. An actual crossing outside its lateral/height
contract refuses to normal movement; it cannot retain stale hop eligibility.
The map and physical support polygons are unchanged.

First run both teams at phase 0, initial force 0/no Speed in the existing
120-second native mode 6 observer, baseline and candidate. If these complete,
run the unchanged 30-case supported resource matrix per arm: both teams,
phases 0/3/6, force/expiry (0/0),(20/0),(50/0),(20/400),(20/8000).
Run at most four isolated native worlds, using accelerated fixed 25-ms outcome
collection. Retain all cases and compare first native pickup/capture times,
idle XY speed<50 and grounded walking XY speed 50..300 through first capture.
Require all existing resource, lifecycle, source/module, clock and stall gates.
This is a complete generated-route development comparison, not the final
defended1x/5x or live-cost qualification. Freeze one fresh native build before
collecting either arm; do not install an unqualified result.

The fresh `strafe-build-supported-intervals-20260906` completes both smoke arms
and all 30 resource cases per arm. All existing native/resource/lifecycle gates
pass. Across the 30 matched first trips, walking falls 59.22→53.32%, while mean
travel changes 56.934→56.827 seconds. Mean idle rises 5.244→5.273%; 16 individual
cases have higher idle and 9 have slower travel. Retain the opt-in mode as a
shared prepared-path implementation; this does not justify default promotion
or a claim of substantially better strafing. The unchanged stopping/cutoff
policy remains a principal efficiency limitation. Paired evidence:
`.codex-research/strafe-supported-interval-resource-audit-20260906/comparison.json`.

Next observe one 600-game-second 5x defended run per team and arm with two forced
CHASE/BASE opponents, at most four isolated worlds. This development check
tests the shared executor under actual bodies/respawns; it is not the declared
final 12-case campaign and cannot close final confinement or timing gates.

All four defended development runs complete. No unexplained distant confinement
is observed, but travel idle rises in both candidate comparisons:
red 6.36→7.45%, blue 6.59→8.46%. Walking is 29.48→29.29% red and 42.12→35.44% blue.
The red candidate also waits 222.75 seconds near home while carrying and its
home flag remains away; the existing audit classifies this as arrival/wait,
not distant confinement. These are single runs in divergent native matches,
not a causal estimate. They do not support default promotion. Retain the
shared executor as opt-in and continue geometry-validated corner/continuation
planning before another final qualification attempt. Full retained evidence:
`.codex-research/strafe-supported-interval-defended-20260906/`.
