# Human landing probes: selected-path rejection

The three demonstrations establish a real momentum-continuation deficit.
Native probes now isolate one contributing mechanism: at the fastest run's
sixth jump state, all six existing immediate straight proposals are rejected
for leaving the selected path. Continuing them observationally passes the
native physical and onward checks. This is not a qualified production fix,
an exact replay of the human, or a sub-15-second bot run.

## Bound evidence

Local evidence is `.codex-research/human-landing-probe-20260917-a/`:
`prepare.py`, `cases.json`, `binding.json`, `summarize.py`, `summary.json`, and
the `control`, `sweep`, `path-audit-on`, `path-audit-off` worlds. Each world
retains commands, settings, server log, sampled trajectory and result.
The source is human `1789686350-0-c0.csv`, SHA-256
`7b273c967ab7d3551599f82a89c1cfde224698e56502eabcb8c5dc4e32b25935`.

Roots use pre-command position, velocity and yaw at jumps 3, 6 and 7
(rows 505, 846, 960; elapsed 4050, 6775, 7675 ms). The native corner fixture
resets controllers, jump/knockback bookkeeping and resource phase. It does
not restore a complete human playerState_t. Bot decisions are 25 ms versus
approximately 8 ms human commands. Initial Force is 100 or 0, with native
regeneration afterward and no authorized Force spend. These are **synthetic
continuation probes**, not interchangeable human and bot roots.

All use the retained `move-explore-20260916-b/seed.txt`, heading offer 1,
replay 3, trigger logging 1, parity 256, serial localhost worlds at timescale
1. No installed files were changed. Builds, with frozen source manifests:

- Control: `move-build-human-route-20260917-b`,
  `20260917T230009711368Z.sources.json`.
- Heading sweep: `move-build-heading-sweep-20260917-a`,
  `20260917T231832857756Z.sources.json`.
- Path audit on/off: `move-build-path-audit-20260917-a`,
  `20260917T232205603794Z.sources.json`.

## Results

Control remaining pickup times, seconds (not full flag-to-flag times):

| Synthetic root | Initial Force 100 | Initial Force 0 |
| --- | ---: | ---: |
| Jump 3, 545.3 units/sec | 16.000 | 16.925 |
| Jump 6, 749.8 units/sec | 12.000 | 12.475 |
| Jump 7, 806.4 units/sec | 8.425 | 7.950 |

Six of six complete with no deaths. Phase-sensitive fixture setup prevents
attributing the pairwise time differences to Force alone.

At jump 6, all six immediate proposals are `off_path`. The chosen 24-unit
run-up reduces launch speed from 749.8 to 486.1 before the jump. Immediate
proposals land at roughly (1079, 1960, 1808), on valid graph cells, retaining
728.0–735.1 units/sec. First-landing safety alone is insufficient to admit
them; the prior log did not tell whether subsequent motion was viable.

`bot_moveHeadingSweep 1` adds 24 proposals: offsets -20, -10, +10, +20 degrees
to both headings of each zero-run-up straight candidate. It preserves the
native checks and ranking; enabled proposals may extend the control walking
reference horizon. Both Force cases complete in 12.075 seconds, but every
new immediate proposal is still `off_path` and the original run-up wins.
Some proposals fall on continued motion. The sweep **did not solve the
root defect** and remains default off.

`bot_moveOffPathAudit 1` reruns rejected proposals after the original judge,
using HopHoldCommand through the native physical/onward checks without the
selected-path early stop. It has its own world guard and local measurements;
it does not alter candidate results, walking references, rankings or graph
learning. Guard failure aborts the search. It adds work and rollout counts.

At the first post-reset root in `path-audit-on` (level 3375), all six original
immediate proposals pass this audit with unsupported=0. The three two-hop
proposals land first at 728.0, 731.3, 735.1 and second at 692.0, 693.0, 695.3
units/sec, both near z=1808. They satisfy onward progress at frame 84 and
complete the 115-frame rollout. The three one-hop proposals also pass.
Other rejected proposals still fail (`off_graph`, `refused`, or `fell`):
removing all path restrictions is not justified.

Audit `outcome=admitted` means **path-free physical/onward checks passed**.
It excludes the original walk-margin comparison, optional state second-edge
entry check, and arrival-trigger exception. It does not prove a connected
new route, full admissibility, or an end-to-end timing improvement.

The audit-on and audit-off latest-build worlds both finish in 11.775 seconds.
Their 240 post-reset candidate records match after normalizing level time.
Sampled CSVs differ (49 versus 51 rows); this is not a full trajectory or
command identity proof. Fixture wall-clock setup remains phase-sensitive.
All ten probe worlds have zero parity mismatches **and zero compared parity
boundaries**; refusals range 113–209. This is no positive parity qualification.

## Verification and next implementation

The production StraightJudge extraction test passes all four scenarios and
rejects its coupled-reference negative control. Its extraction now ends at
the next static function, excluding the new audit helper. Retained passing
artifacts: `movement-reference-test-20260917T232539Z-ba6fd5e9`; the earlier
compile failure remains in `movement-reference-test-20260917T232241Z-2c9ffea0`.
All 48 movement replay tests pass. Native audit source received an independent
read-only review; no isolation blocker was found. No full floor campaign was
run for these default-off diagnostics.

The next implementation should construct and validate an alternative onward
path from a candidate landing, carry that path into forecast and execution,
and compare the complete continuation on its own walking reference. Retain
native fall/trigger/unsupported/landing/onward checks and chain admission.
Do not merely widen the 48-unit path tolerance or skip off_path admission;
the rejected set contains both viable and failing trajectories. Keep human
coordinates confined to fixtures, not production navigation.

First reproduce the six immediate candidates at jump 6, verify their new
path connection and references, and demonstrate an executed high-speed
continuation. Then test jumps 3 and 7, matched stationary starts, the affected
safety floor, and fresh parity with actually compared boundaries. Report
stationary pickup time separately from remaining synthetic-root time. The
original fastest-run search remains incomplete until that end-to-end result
is measured; local distance-over-walk margin is not a fastest-time objective.
