# Replay review corrections, 2026-09-17

This record supersedes the timing attribution, blanket heading rejection,
single-cause conclusion and next experiment in [the earlier replay record](2026-09-17-replay.md).
Original native logs and analyzer-v1 outputs remain intact. No native
sources or gameplay settings were changed by this correction.

## What changed

`tools/movement_replay.py` is analyzer version 2. Historical `launch`
events confirm the previous command's takeoff: `HopStep` sets `launchTime`
when issuing the tap, then increments its stage when the next command
observes the resulting airborne state. Treating confirmation as takeoff
understates flight by one command frame and invents ground time at an
immediate chained tap.

For these retained fixed-step worlds the verified delay is 25 ms. Version
2 requires the explicit `--launch-confirmation-delay-ms 25` binding for
historical logs; it does not assume every run has this cadence. It also
supports an explicit future `launch_level` event field, which takes
precedence. It preserves the original confirmation timestamp. Launch
speeds/positions remain confirmation-frame observations: shifting their
timestamp does not reconstruct the launch state.

Unjudged A/B outcomes are now null admissions and counted separately as
**censored**, rather than counted as failures. The control-only walk
horizon can leave a B rollout unjudged. No conclusion about its admission
is warranted until an independent walk reference judges it.

The partition also no longer double-counts time when a second launch is
observed without an intervening landing. Its fallback labels still classify
residual intervals by event order; a zero arithmetic remainder does not
prove that every interval's physical cause is known.

## Recomputed evidence

All outputs and SHA-256 input bindings are in
[the correction manifest](../../../.codex-research/move-replay-correction-20260917T061951996008Z/manifest.json).
Each case subdirectory contains `replay.json`, `report.txt`, `segments.csv`,
`transitions.csv`, `paired_arms.csv`, `runup_pairs.csv` and `chain_breaks.csv`.

| Large-seed stationary outbound | Red | Blue |
|---|---:|---:|
| Pickup elapsed (unchanged) | 19,150 ms | 19,925 ms |
| Corrected hop flight | 16,225 ms | 16,525 ms |
| Remaining intervals | 2,925 ms | 3,400 ms |
| Prefix | 450 ms | 450 ms |
| Commit-to-launch approach | 500 ms | 500 ms |
| Search/walk intervals | 1,975 ms | 2,450 ms |
| Artificial inter-hop ground removed | 200 ms | 175 ms |

Relative to the red path's 12,925 ms flight and 412 ms walk estimate,
the 5,813 ms gap is **3,300 ms extra flight plus 2,513 ms extra residual
time**. This is accounting across different executed/planned sequences,
not proof of one cause. The earlier 2,800/3,013 decomposition is withdrawn.

| Input | Pairs | Both admitted | A only | B only | Neither | Censored |
|---|---:|---:|---:|---:|---:|---:|
| Large-seed red | 44 | 9 | 1 | 0 | 33 | 1 |
| Large-seed blue | 52 | 6 | 2 | 2 | 40 | 2 |
| Small clean-seed red | 44 | 10 | 3 | 1 | 28 | 2 |

All three inputs have zero unpaired candidates. The B-only cases are
real local admissions; B was observe-only, so they do not demonstrate a
faster executed route. The heading hypothesis remains open.

## Exact cases for the retry

Use the input server logs under `.codex-research/` and their corresponding
v2 case reports. Search IDs are local to each case/run.

| Input run | Case | Search | Engine level | Edge | Hops / runup | A / B |
|---|---|---:|---:|---:|---|---|
| `move-replay-20260917T040150120Z-stands` | `blue_out_L1_stand` | 19 | 18950 | 78278 | 1 / 24 | no_second_landing / admitted |
| same | same | 23 | 20400 | 93195 | 2 / 0 | refused / admitted |
| `move-replay-20260917T040150120Z-red-probe` | `red_out_L1_stand` | 19 | 20400 | 3999 | 2 / 24 | refused / arrives |

Inspect candidate construction, arrival truncation and complete issued
policy when following these IDs: the logged final hop count alone does
not establish identical original construction. Censored pairs are present
in `paired_arms.csv` as blank/null admission values.

## What the successor should do

1. Read these corrected reports before selecting a fix. Reuse the native
   evidence; do not rerun old campaigns merely to rebuild context.
2. Investigate the three B-only candidates and judge censored B candidates
   with their own sufficient walk reference under the same full native
   root/guard. Keep the control's walk reference, ranking and committed
   parity frames unchanged. Copy reused forecast buffers before another
   rollout. Then test any promising stored-heading proposal as a separate
   opt-in treatment, with repeated matched route outcomes.
3. If transition merging remains implicated, capture it prospectively.
   `Graph_AddStrafeEdge` discards losing records; a final cache and the
   traversal count cannot recover their exits. Log every incoming record,
   existing record, decision, immutable record version and relevant root
   context before mutation. Use fresh uniquely bound preparation only if
   existing complete captures cannot supply those records.
4. Compare transitions from matched actual entries; differing exits from
   differing roots are not proof of harmful merging. Demonstrate that a
   controlled preservation of a distinct transition restores composition
   and improves native execution before promoting a merge-policy change.
   Preserve the safety gates and full level-1 qualification requirements.

Native pickup timing, survival results and the outstanding no-legacy and
greedy-performance failures remain as reported. Neither experimental cvar
is promoted. The sub-15-second target remains unmet. Full-state persisted
replay and immutable merge histories were not delivered by the previous
telemetry; its selected root fields must not be described as a complete
reproducible player-state capture.

## Reproduction and checks

From the workspace root, choose a fresh output directory (never overwrite
the retained v1 or v2 outputs):

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
$correctionOut = '.codex-research/replay-v2-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
python tools/movement_replay.py .codex-research/move-replay-20260917T040150120Z-red-probe/red_out_L1_stand .codex-research/move-replay-20260917T040150120Z-stands/red_out_L1_stand .codex-research/move-replay-20260917T040150120Z-stands/blue_out_L1_stand --launch-confirmation-delay-ms 25 --out $correctionOut
if ($LASTEXITCODE -ne 0) { throw 'Replay analysis failed; preserve output.' }
python -m unittest discover -s tools -p test_movement_replay.py
if ($LASTEXITCODE -ne 0) { throw 'Replay analyzer tests failed.' }
```

The affected suite passes 22 tests, including command-boundary timing,
immediate chaining, missing landings, input preservation, explicit cadence
requirements and censored comparisons. These are analysis corrections;
no new native performance or qualification result is claimed.
