# Dantooine control extension: batch 2 on 48 seeds (2026-11-10)

Owner, 2026-11-10: "work on what u can". Batch 2's Dantooine result on 16 seeds (35-50) had a blue-ward side shift
(red minus blue -3.26 ± 0.84) that looked like the control's seeds ([record](2026-11-10-review-batch2.md)). This
adds seeds 51-82 for both arms, so the shift is settled and later Dantooine arms have 48 control seeds.

## Setup

- `nd`: build `review-next-build-20261110-g`, the old (new-defaults) control's binary and defaults.
- `b2`: build `review-next-build-20261110-k`, defaults (batch 2 on since a971ed85); it replays the batch-2 arm's
  worlds (build -j, every fix set to 1) exactly.
- Dantooine seeds 51-82, 4v4 laps, 600 game s, rate 25, meter on. 64 worlds, none failed.
- Evidence: `.codex-research/danx-20261110-a/` (`runarms.log`, `compare-dan-35-82.txt` pooled with the earlier
  seeds 35-50, `compare-dan-51-82.txt` the new seeds alone, `worlds/{nd,b2}-dan-51..82`).

## Results (b2 minus nd, per 600 game s)

| | seeds 35-50 (16) | seeds 51-82 (32) | pooled 35-82 (48) |
|---|---|---|---|
| team captures | +0.57 ± 0.39 | -0.31 ± 0.26 | **-0.02 ± 0.22, null** (inside ±15%) |
| runners' falls | +0.05 ± 0.71 | -0.15 ± 0.41 | -0.08 ± 0.36 |
| blue captures | +2.20 ± 0.58 | -0.17 ± 0.45 | +0.62 ± 0.38 |
| red minus blue | -3.26 ± 0.84 | -0.37 ± 0.80 | -1.34 ± 0.61 |
| camper falls (blue camper) | +0.18 ± 0.44 | **+1.08 ± 0.31** | **+0.78 ± 0.26** |

- Team captures: batch 2 is a null on Dantooine over 48 pairs.
- Side shift: on the new seeds it is -0.37 ± 0.80. Most of the 16-seed shift was those seeds; the pooled -1.34 ±
  0.61 does not pass the effect cut.
- **Camper falls rise.** `camper_falls` counts the blue camper (`Camper_v2`). On seeds 51-82 its falls go from 37 to
  72. Pushed off goes from 17 to 41 (by `Chaser_v2` 8 → 25, by `Defender_v2` 9 → 16) and self falls from 20 to 31.
  The red camper's falls do not rise (70 → 60). The extra falls are in red's end: the 256-unit cell around
  (5120, -768) goes from 3 to 11, and (5376-5888, -512..0) adds more. The camper's share of live samples in red's end
  (x ≥ 4900) is unchanged (46.9% → 44.9%), so it does not go there more often; it is knocked off more often while
  there. The 16-seed split ([record](2026-11-10-review-batch2.md)) put most of the rise in the movement half
  (`b2m` +0.61 ± 0.43, `b2d` +0.06 ± 0.42). The scripts are in `danx-20261110-a/analysis/` (`camperfalls.py`, `campertime.py`); the counts are from
  `play-game.log` `MOD_FALLING` kill lines and the play CSV.

## Next

An attribution run on the same 32 seeds (`camperfall-20261110-a`): `nomove` (batch-2 movement fixes off) and `nobm5`
(BM-5 alone off), build -l against these `b2-dan` worlds. BM-5 is the first suspect: it holds the objective while a
knock recovery runs, and the extra falls follow pushes.

`danx-20261110-a/worlds/b2-dan-51..82` with `batch2-20261110-a/worlds/b2-dan-35..50` is the 48-seed Dantooine
control for later arms.
