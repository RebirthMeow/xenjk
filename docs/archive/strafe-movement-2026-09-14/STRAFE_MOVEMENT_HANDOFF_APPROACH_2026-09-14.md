# Native approach to a prepared path

The owner authorized the missing off-line entry capability from the
[handoff redesign](STRAFE_MOVEMENT_HANDOFF_CONTRACT_2026-09-14.md).
It is implemented and exercised in native play. The latest eight-case cohort
captures alive in all eight worlds with zero audited prolonged confinements.
This closes the targeted entry milestone, not overall movement acceptance.

## Controller change

An unowned, world-grounded player moving below 80 units/s can propose a walking
connection from its actual position to a prepared straight interval. The
connection is 32–128 units long with at most eight units of endpoint height
difference. The proposal excludes incoming arc/speed/pass contracts and pass
rejoin intervals. Every downstream point and its contract is copied unchanged.
No map coordinates, route IDs or resource grants are used.

The new first interval is non-hopping, limited to 250 units/s and retains a
stopped turn at the join. A newly admissible direct entry may supersede it only
through the existing position, velocity and native admission checks. The native
owner checks the whole connection with four floor probes and four standing-hull
step sweeps, up to 32 units between samples, rejecting unsupported floor,
non-world support, excessive steps, obstruction, lava/slime and `trigger_hurt`.
It shares the existing 32-trace budget. A failed proposal never acquires command
ownership or overwrites an active path.

The executor owns the committed approach and follower. Preparation cannot
restart an active approach or reset its progress by offering replacements.
Destination adapters are withheld until the approach joins the prepared path;
frontier and body-obstruction behavior remains in force. Fast or airborne
off-line states still yield to existing recovery/fallback. This is a bounded
walking entry, not a general high-speed interception planner.

## Verification

Build: `.codex-research/strafe-build-handoff-approach-20260914-a`, manifest
`20260914T051646221819Z.sources.json`.
Study: `.codex-research/handoff-approach-20260914-a`.
The preceding candidate C's 1107 native source inputs were hash-verified and
copied to `baseline-source/` before edits. Its native cohort remains under
`.codex-research/handoff-contract-20260914-a/candidate-c`.

The original eight-case `cohort.json` is unchanged. Native sources were frozen
through build and collection. Both teams, two ordinary-spawn seeds and two
zero-Force/no-initial-Speed fixture repetitions are retained, including full
observation windows after capture. No new initial-state intervention or dropped
flag case was introduced. Case settings match except declared ports. Current
sources, staged binaries and fixtures are verified in `comparison.json` and
`verification.json`.

Passing focused checks:

- `kernel-check/`: eight rotations, both lateral signs, real executor command
  acceptance, unchanged downstream geometry, and fast/air/high/distant/pass
  refusal. Existing snapshot/frontier/overspeed checks also pass.
- `owner-check/`: native admission refusal, retained active approach/progress,
  destination-adapter exclusion and command restoration.
- `clearance-check/`: extracted native query logic rejects a final-sample
  obstruction, floor/contact/slope/liquid/hurt failures and exhausted budget;
  it checks the whole 128-unit connection and standing-hull dimensions. Trace
  replies are controlled, so this is query-contract evidence, not engine physics.
- `retained-entry-check/`: the original blue episode's prepared nodes at level
  57600 and observed stationary position at 57775 reject direct entry but admit
  an approach proposal. This is retained geometry, not exact native state replay.
- `path-regression/`: real shared executor release and crossing contracts pass.

## Native results

| Measure | Previous C | New approach |
| --- | --- | --- |
| Worlds completing and capturing alive | 8/8 | 8/8 |
| Worlds without audited prolonged confinement | 7/8 | 8/8 |
| Affected blue fixture: grounded 3D idle share | 9.13% | 2.28% |
| Zero-resource first returns: traced grounded XY idle | 0 in all four | 0 in all four |

Seven approach commits are recorded across seven worlds. Six have at least two
seconds of subsequent CSV observation: displacement is 404–485 units with no
supported release in those windows. The seventh is near the end of collection
and lacks the full two-second window; retain that censoring. See
`approach-outcomes.json`. In the affected blue fixture the approach commits
after the second capture at level 54275 and subsequent sampled displacement is
404 units. This supports useful native continuation, without claiming an exact
replay or sole causal attribution of timing changes.

| Case | C first observed / capture level (s) | Approach first observed / capture level (s) |
| --- | --- | --- |
| Spawn red r1 | 4.325 / 36.900 | 1.800 / 31.550 |
| Spawn blue r1 | 4.350 / 31.550 | 1.800 / 32.000 |
| Spawn red r2 | 4.350 / 36.750 | 1.800 / 31.825 |
| Spawn blue r2 | 4.325 / 36.925 | 1.800 / 34.125 |

Native startup/observation phases differ. These are observed boundaries, not
proof of matched spawn-to-capture gains. First-return fixture times are likewise
mixed: red r1 22.200→22.150, blue r1 22.250→22.325, red r2 22.300→22.150,
blue r2 22.250→22.150 seconds. Each fixture first return has 50 ms uncovered by
the XY trace. Whole-observation idle/walking also remains mixed; for example
ordinary blue r1 walking rises 32.94→35.68%, while red r2 falls 39.23→34.90%.

## Remaining work

The off-line walking approach is complete for the declared bounded domain.
Do not report eight successful worlds as overall movement acceptance. The
unchanged whole-trip idle/walking and speed gates still lack a clean pass.
Use consistent native start/observation anchors and retain actual resource and
movement-clock phases before attributing small trip differences to policy.
Continue ordinary-route walking/turn analysis from the retained command owners,
then perform broader body, defended and cost qualification when movement gates
pass. No installation, dropped-flag retrieval or threading work was performed.
