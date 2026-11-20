# M1 route catalog experiment

Hypothesis, frozen before native runs: one server route asset can hold both
verified Kejim guides and select the correct guide from the bot's team and
nearby entry, preserving the accepted per-route movement traces.

The catalog uses `STRAFECATALOG1 <count>` followed by inline `STRAFEROUTE1`
guides. Existing standalone guides remain supported. Limits are 32 guides,
1,024 steps per guide and less than 512,000 file bytes. A malformed member,
wrong map checksum or trailing data rejects the whole catalog. The adapter
loads and preprocesses once per configuration/map; inactive bots consider
same-team entries within the existing 64-unit acquisition radius and select
the nearest, breaking equal-distance ties by file order. Active followers
retain an independent route index. Existing acquisition, objective, physics
and release gates remain in force. Emptying/changing the route cvar or map
restart clears cached follower state.

## Frozen evaluation

- Original red route: all 12 accepted movement/command traces must remain exact,
  with one acquisition/departure and no release.
- Blue respawn 3: planned native reference plus all eight runtime phases;
  retain the M1 110% nominal / 120% worst-phase limits. Compare runtime traces
  against the already accepted blue qualification as well.
- Both runtime batches stage the exact same catalog bytes and cvar value.
- An unsupported flagstand start must not acquire either guide. Malformed
  catalog input must not execute a valid prefix. Keep the failed pickup or
  timeout as diagnostic evidence, separate from the rejection check.
- A standalone-guide smoke run checks compatibility.
- Red respawn 2 also gets a separate native-heading qualification: its authored
  90-degree heading, a same-start planned native reference and eight runtime
  phases under the catalog. This closes a previously unmeasured part of M1;
  the original prepared-heading regression remains a separate requirement.

These are solo native route-selection trials. They do not establish M3's
8-bot cost budget, interactions between live players, new spawn coverage,
or fast flagstand traversal. Aiming remains paused.

Catalog candidate: `.codex-research/strafe-catalog-candidate-20260904`.
Clean build: `.codex-research/strafe-build-catalog-20260904`.

## Results

The catalog passes the frozen selection checks. Its exact bytes are preserved
as `tools/fixtures/strafe_flag/kejim_catalog.txt`; route 0 is red-i2 and route 1
is blue-i3. Every runtime trial acquired the correct route once. The original
red 12-case regression retained all **3,248** movement/command checkpoints;
the blue eight-phase regression retained all **2,148** checkpoints. Both used
the identical `flag_runtime.txt` cvar and catalog bytes.

| Native spawn | Planned | Nominal | Slowest of eight phases | Individual M1 gate |
|---|---:|---:|---:|---|
| Red respawn 2, authored yaw 90 | 6.600 s | 6.775 s | 7.900 s | pass |
| Blue respawn 3, authored yaw 270 | 6.600 s | 6.800 s | 6.800 s | pass |

Red phase 3 is only 20 ms inside the 7.920 s worst-phase threshold. Keep this
case in later recovery evaluation; deterministic phase coverage does not
establish a statistical reliability margin. Planned and nominal captures for
both poses have identical starting origin, velocity, view angles, ground
entity, force amount and active force powers. Full player-state equality is
not assumed.

The red flagstand still times out at 20 s without acquiring either guide.
A catalog with a valid first entry and wrong-checksum second entry loads no
route. A wrong-team standalone guide loads but is never acquired by red.
Both rejection probes time out under fallback, which is preserved as an
outcome rather than counted as traversal success. A legacy standalone-guide
run still picks up at 6.700 s with all 268 original checkpoints unchanged.

The compiled production loader/selector check passes team, distance and
stable-tie selection; shared cache and per-client reset checks; map/config
invalidation; 11 malformed inputs; and standalone compatibility. It uses a
stub engine API and does not replace the native collision/movement runs.
All 42 Python tests pass, and a clean dedicated engine/game build succeeded.

Audit: `.codex-research/strafe-catalog-evaluation-20260904/summary.json`.
It checks **34 native trials and 712 source/staged-artifact hashes**, including
one game module across all trials. Batch artifacts:

- `.codex-research/strafe-catalog-red-regression-20260904`
- `.codex-research/strafe-catalog-blue-qualification-20260904`
- `.codex-research/strafe-catalog-red-qualification-20260904`
- `.codex-research/strafe-catalog-probes-20260904`
- `.codex-research/strafe-catalog-check-20260904`

The guide cache reserves 2,490,832 bytes plus a 512,000-byte input buffer;
loading/preprocessing happens on first eligible dispatch after map/config
change. The pure loader check verifies that subsequent bots do not reread the
file. Native solo execution passes; simultaneous live-player interactions and
the full M3 workload remain unmeasured.

M1 remains in progress: two of 24 spawn poses meet the individual speed/phase
gate, and neither flagstand start has a fast qualified route. The next route
work should include flagstand traversal, retain both current entries and
expand coverage without changing per-team runtime configuration.
