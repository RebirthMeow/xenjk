# The stored-heading offer, measured, 2026-09-17

Work order step 3: test the smallest successor change the audited pairs
support, with matched controls, and keep every failure. This record is that
measurement. **The offer is not promoted, not defaulted and not installed.**

Read [the rescue audit](2026-09-17-rescue-audit.md) and
[the arm-B reference record](2026-09-17-arm-b-reference.md) first. This record
depends on their result that neither heading construction dominates.

> Follow-up: [independent-reference correction](2026-09-17-independent-reference.md) supersedes the shared-reference implementation and its monotonic-ranking claim. The measurements below remain historical results of the original offer build.

## The change under test

`bot_moveHeadingOffer`, default off. Unset or zero is exactly today's
behaviour: one construction, `AimAt(live root, the edge's stored landing)`.
Nonzero arms the stored-heading construction (`graphEdge_t.dir` for hop 0 and
hop 1) and makes it an **ordinary proposal** rather than an observe-only
measurement:

- judged by the same admission rules, hold, safety checks and spend policy;
- sharing the control's single walk reference and its horizon;
- counted in `stateAdmitted`, so an edge the stored heading validates is not
  avoided and does not force a replan;
- ranked with the control alternatives by margin over the walk, and eligible
  for the `bot_moveStatePrefer` planned-chain preference on the same terms.

Only candidate construction and eligibility change. No entry gate, reach band,
takeoff bound, hold length or onward distance was touched, no exclusion was
relaxed, and no Force is spent. The control alternatives are retained, which is
the intended local comparison. This did not guarantee unchanged control
judgments: the shared longer walk could invalidate an earlier valid prefix.
Local margin also does not guarantee faster route completion.

## Build and campaigns

Build `.codex-research/move-build-offer-20260917T070254469Z`, one binary for
both arms; the arms differ only in the cvar. `bot2_move.c` compiles with no new
warnings. Every campaign below used `--edges --parity 256 --parallel 1`, one
world at a time. Outputs under
`.codex-research/move-offer-20260917T070254469Z-*`.

## Stand cases, three matched repetitions per arm

Large seed, `--filter '^(red|blue)_out_L1_stand$'`. All 12 runs passed and
completed, with zero deaths, zero forbidden Force spend and zero parity
mismatches over a 256-frame horizon.

| Case | Control | Offer | Median change |
|---|---|---|---:|
| `red_out_L1_stand` | 20,700 / 20,700 / 20,175 (median 20,700) | 19,575 / 19,250 / 19,075 (median 19,250) | **-1,450 ms** |
| `blue_out_L1_stand` | 19,925 / 20,650 / 19,925 (median 19,925) | 20,525 / 19,800 / 20,525 (median 20,525) | **+600 ms** |

Red separates completely: every offer run is faster than every control run, by
925 to 1,625 ms. Blue does not separate: its two distributions overlap and the
offer's median is 600 ms slower. Three repetitions per arm is a small sample
against a level-1 route spread already recorded at up to 2,050 ms, so the blue
result is indistinguishable from no effect and the red result is suggestive
rather than settled.

**Neither case is close to the 15,000 ms target.** The fastest run in this
record is 17,500 ms, on a return case, which is not the timing experiment.

## Named regression cases

| Case | Control | Offer |
|---|---|---|
| `red_ret_L1_stand` | 22,550 ms, `move` | 18,750 ms, `move` |
| `red_ret_L1_respawn4` | 19,625 ms, `move` | 19,850 ms, `move` |
| `blue_out_L1_respawn5` | 20,825 ms, `jumppad,move` | 17,575 ms, `jumppad,move` |

All six pass and complete with zero deaths, spend and parity mismatches. The
former fatal `blue_out_L1_respawn5` case survives in both arms; its `jumppad`
owner is the map's jump pad, not the legacy movement stack.

## The 12-case seeded level-1 arm

Large seed, no filter, one campaign per arm. Both are 12 of 12 passing with
zero deaths, zero forbidden spend, zero parity mismatches and zero judged-short
candidates.

| | Control | Offer |
|---|---|---|
| Owners over the arm | `jumppad`, **`legacy`**, `move` | `jumppad`, `move` |
| Mean against the offer arm | +508 ms | — |
| Mean against `move-review-20260916-seeded-b` | -338 ms | **-846 ms** |
| Mean against the greedy reference `move-phase3-20260916-speed-f` | **+1,748 ms** | **+1,240 ms** |
| Faster than greedy | 2 of 12 | 4 of 12 |
| Stand family mean | 21,075 ms | 19,944 ms |

The offer arm is faster than the control arm on 8 of 12 cases, mean -508 ms,
range -2,050 to +900 ms.

**The offer arm does not pass the level-1 gate.** The gate is no legacy owner
*and* no slower than the retained greedy arm. The offer arm is still 1,240 ms
per case slower than greedy, so it fails, by less than the control's 1,748 ms.

The control arm's single `legacy` owner is `red_ret_L1_stand`, the known
failure. **Do not read the offer arm's clean ownership as a fix.** The same
case, the same build and the same settings produced `move`-only ownership in
the regression campaign above at 22,550 ms, so that case's ownership varies
between campaigns. One campaign per arm cannot attribute it.

## The 36-case unseeded floor

Default configuration, `bot_moveHeadingOffer` unset: **36 of 36 pass, zero
parity mismatches, zero judged short, `move` the only owner.** The floor is
where it was before this milestone's changes, which is the check that the new
diagnostics, the `no_onward` label, the arm-B reference and the offer's dormant
code did not move the default.

## What this does and does not establish

Established:

1. The offer is safe at level 1 on this map: 36/36 floor, 12/12 seeded, three
   named regression cases, six stand repetitions, with no death, no forbidden
   spend and no parity mismatch anywhere.
2. It is faster than its own control on the seeded arm by 508 ms per case and
   on the red stand case by 1,450 ms at the median, with complete separation on
   red at three repetitions.
3. It narrows the deficit against the greedy reference from 1,748 to 1,240 ms
   per case.

Not established:

1. **It does not pass the level-1 gate.** It remains slower than greedy.
2. It is not shown to help blue, where it is 600 ms slower at the median with
   overlapping distributions.
3. It is not shown to fix `red_ret_L1_stand`'s legacy ownership; that case's
   ownership varies between campaigns of the same build.
4. **No sub-15-second stationary no-Force pickup exists in this record**, in
   either arm, on either primary case.

So `bot_moveHeadingOffer` stays default off. It is a measured, reproducible
improvement over its own control that is not yet an improvement against the
bar, and it should not be defaulted on this evidence.

## Reproduction

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
$b = '.codex-research/move-build-offer-20260917T070254469Z'
$large = '.codex-research/move-explore-20260916-b/seed.txt'
$out = '.codex-research/move-offer-check-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
python tools/movement_bench.py --build $b --out ($out + '-offer') --level 1 --graph $large --edges --parity 256 --set bot_moveHeadingOffer=1 --parallel 1 --port 29968
python tools/movement_campaign_compare.py ($out + '-offer') `
  --against control=.codex-research/move-offer-20260917T070254469Z-seeded-control `
  --against greedy=.codex-research/move-phase3-20260916-speed-f --no-baseline --cases
```

## Next executable step

1. Repeat the blue stand case enough times, in both arms, to say whether the
   offer's blue median is a real cost or route quantisation. Three repetitions
   cannot.
2. Follow the remaining deficit against greedy where it is, not where it is
   convenient: the seeded arm's stand family is 19,944 ms against greedy's
   20,381, so the deficit is in the respawn families (18,000 against 15,356 at
   respawn5). That is a different route problem from the one this milestone
   examined.
3. Only then consider the onward test, as its own controlled experiment with
   the falling-endpoint regression kept: 121 `no_onward` candidate refusals in
   one 12-case arm is a large class, and it is measured, not conjectured.
