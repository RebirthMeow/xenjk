# Mirror-twin audit of the eight maps (2026-09-29)

The eight maps added on 2026-09-27 ([eight maps](2026-09-27-eight-maps.md))
were never entered in `movement_bench_lib.TWIN_SKIPPABLE`, so
`test_every_fixture_map_is_measured` had failed since then and every
`--skip-twins` arm on those maps ran all 36 cases. No result was affected: an
unlisted map runs both twins.

## Audit

Same test as [2026-09-25](2026-09-25-deferred-pin.md#mirror-twins): within
each retained class-pinned arm, pair `blue_ret_X` with `red_out_X` and
`red_ret_X` with `blue_out_X`, and compare status and `time_ms`. Arms read by
exact path (arm-level `result.json` only): `maps8-20260927-a`,
`maps8-meshedges-20260927-a`, `mapprep-20260927-a`, `material-20260927-a`,
`dropseed-20260927-a`, `droparc-20260927-a`, plus the Kejim and first-four
arms of `native-default-20260927-{j,k}`, `padmatch`, `navoff`, `hopprice`,
`defender-20260927-a` and `gate-20260928-{a,b}`. Classes 50, 100 and 150;
builds `native-build-20260927-{k,l,m}`.

| Map | Arms | Twin pairs | Identical |
| --- | --- | --- | --- |
| Illimiran v2 | 5 | 90 | 90 |
| Kharzet v3 | 6 | 108 | 108 |
| Kothis v5 | 6 | 108 | 108 |
| Manaan | 6 | 108 | 108 |
| Nelvaan | 6 | 108 | 108 |
| Talay | 11 | 198 | 198 |
| Talravin | 11 | 198 | 198 |
| Telti | 6 | 108 | 108 |
| Kejim (cross-check) | 28 | 168 | 161 (the 7: `L1_stand`, already kept) |

1,026 of 1,026 pairs on the eight maps are identical. Kejim's differences are
all stand starts, which the table already keeps, so the audit reproduces the
2026-09-25 finding.

## Change

All eight maps are added to `TWIN_SKIPPABLE` with no keep pattern. The Python
suite is green again (936 tests). `--skip-twins` now halves the eight maps'
comparison arms as it does the others; gates and floors still keep both twins.
