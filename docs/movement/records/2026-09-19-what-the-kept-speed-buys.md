# What the kept speed buys, 2026-09-19

Corrected 2026-09-20 after review. The retained outbound trace supports an
association between remaining grounded and losing speed. It does not establish
that heading never matters, or that a particular admission change saves time.

## Correction and reproducible binding

The original analysis included pre-fixture and post-pickup activity. Its
67-landings table, 31-grounded breakdown, 29-of-36 chain claim and assertion
of three distinct trajectories are withdrawn. The earlier 4 Hz distance-bin,
correlation and fast/slow tables are also withdrawn pending a reproducible,
leg-bounded analysis; neither a coarse bin nor its mean establishes identical
geometry or rules out heading as a discriminator. The original document is
retained in [the review snapshot](../../../.codex-research/momentum-review-20260920-a/original-docs/records/2026-09-19-what-the-kept-speed-buys.md).

No native runs were needed for this correction. The three retained worlds are
`.codex-research/mom-on-r1..r3/red_out_L1_stand`, built with
`move-momentum-build-a`, on the unguided no-spend stationary red outbound case.
[The audit](../../../.codex-research/momentum-review-20260920-a/audit-v2.json)
binds each result, server log and play CSV by SHA-256 and contains each accepted
landing, its window, committed hop count and category.

Reproduce with a fresh output path:

```powershell
python tools/movement_momentum_audit.py .codex-research/mom-on-r1/red_out_L1_stand .codex-research/mom-on-r2/red_out_L1_stand .codex-research/mom-on-r3/red_out_L1_stand --out .codex-research/momentum-review-NEW/audit.json
python -m unittest discover -s tools -p test_movement_momentum_audit.py -v
```

The first carrying play sample bounds pickup from above; it is not an exact
pickup clock. The audit conservatively ends at the preceding noncarrying
sample. A landing must be strictly after fixture placement, and **every 25 ms
sample through t+500 must exist and be no later than that safe endpoint**.
The fixture's own level is excluded because its momentum row can precede
placement within that level.

| world | fixture, exclusive | safe end, inclusive | first carrying sample | included landings |
| --- | ---: | ---: | ---: | ---: |
| mom-on-r1 | 3325 | 22325 | 22475 | 19 |
| mom-on-r2 | 3325 | 22325 | 22475 | 19 |
| mom-on-r3 | 3275 | 22550 | 22625 | 20 |

Of 70 whole-log landings, one is before the fixture and eleven lack a proven
full outbound window. The old 67-row cohort excluded only three incomplete
log windows, so nine additional rows leave that cohort: one pre-fixture and
eight after pickup. For example r2's level3300 landing precedes fixture3325;
r1's level23675 landing follows the first carrying sample22475.

## Repetitions and speed

The **760 parsed MOMENTUM rows in the safe outbound window of r1 and r2 are
identical**, including command fields. Their 19 landing events also match.
Their differing whole-log totals came from activity outside that window.
There are two distinct observed outbound traces, not three independent
trajectories. The pooled numbers below count the duplicated trace twice;
the distinct-trace view keeps r1 and r3 once each. Neither view makes individual
landings independent statistical samples.

| cohort and next-frame state | n | landing | +25 ms | +50 ms | +100 ms | +200 ms | +500 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| pooled, airborne | 33 | 403.7 | 403.9 | 406.0 | 410.9 | 422.0 | 455.6 |
| pooled, grounded | 25 | 445.4 | 342.9 | 273.6 | 257.3 | 271.3 | 305.7 |
| distinct traces, airborne | 22 | 399.0 | 399.1 | 401.3 | 406.5 | 418.2 | 453.2 |
| distinct traces, grounded | 17 | 445.6 | 343.5 | 274.1 | 256.8 | 270.9 | 306.8 |

Speeds are units/sec. The grounded group's mean loses about 102.5 units/sec
in the first 25 ms (102.1 after removing the duplicate trace), and approaches
the walk's 250 wishspeed by 100 ms. The airborne group's mean retains speed.
These are group observations consistent with ground friction and walking;
they do not prove a universal per-landing rule or isolate friction from
all other command/collision effects. The old median-next-commit and causal
ownership assertions are withdrawn: commit delay alone does not identify
the controller acting between events.

## Which landing is the end of a sequence?

`hop0` alone is not a chain label. A one-hop plan ends at hop0; only hop0 of
a committed two-hop plan is in the middle. The audit associates each landing
with its preceding sequence commit and reads `hops` from that commit.

| sequence position | pooled airborne / total | distinct-trace airborne / total |
| --- | ---: | ---: |
| middle of committed two-hop sequence | 26 / 26 | 18 / 18 |
| end of sequence, including one-hop plans | 7 / 32 | 4 / 21 |

Every grounded landing here is at a sequence end. There is no observed
within-chain failure in these windows. This replaces the invalid 29/36
comparison; the earlier table mixed one-hop endings with chain interiors.

## What the grounded landing frame did

| action | pooled grounded landings | distinct-trace grounded landings |
| --- | ---: | ---: |
| committed a new sequence | 13 | 10 |
| searched and admitted nothing | 5 | 3 |
| no recorded straight search | 7 | 4 |

All 13 pooled grounded commits select a 24-unit run-up and reissue `up=0`;
all seven airborne commits select zero run-up and reissue `up=1`. Eleven of
the thirteen grounded commits have no admitted tap-now proposal; two have
one that loses selection. With duplicate traces removed those counts are
eight and two. An admitted proposal is forecast evidence, not a measured
alternative execution. No recorded straight search means no commit or failed straight-search result on the
landing frame; replay diagnostics were off, so this is not proof that every
other search path was absent. The missing reason needs instrumented evidence.

## Next and limits

Investigate the **four distinct-trace** landings without recorded straight search (seven pooled),
then the eight distinct grounded commits with no admitted tap-now alternative.
`ReplaySkip` needs `bot_moveReplay=1`; check its instrumentation effect before
using new timed outcomes. Preserve pit refusals and unknown trigger stops.
Track physical next-frame airborne state, committed plan length and run-up,
not an undefined joins-kept count. Qualify any proposed change by complete
outbound times under sequential alternating controls.

The traced benchmark times remain 19,150 / 19,150 / 19,350 ms versus untraced
19,350 / 19,150 / 19,150 on the same build. This limited check found no timing
regression; it does not establish instrumentation invariance generally.
The fixture-only momentum flag gate was widened in `Bot2_FastReturnMomentumTrace`.
`fwd/right/up` are observed before movement think, so use commit `reissued_up`
when discussing that frame's newly issued tap. This is one case and one map,
with two distinct observed trajectories and conservatively bounded windows.
