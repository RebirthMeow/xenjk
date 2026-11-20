# Human route recording

The owner offered to demonstrate faster stationary flag-to-flag routes.
The recorder observes human commands at the authoritative Pmove boundary and
retains pre-Pmove, post-Pmove and post-touch state. It never supplies player
inputs or changes movement/resources. First effective movement starts the
native level clock; enemy flag possession closes a successful attempt.
Menu/chat-suppressed commands cannot start the clock. Manual stop, death,
disconnect and other interruptions remain explicitly incomplete evidence.

Use [HUMAN_ROUTES.md](../HUMAN_ROUTES.md). The isolated launcher stages the
game DLL and dedicated executable, copies the user's config to a private
home, and binds F8/F9/F10 there. Installed game files remain unchanged.
The analysis tool exports a report and native-timestamped route polyline.
Human route geometry is diagnostic evidence, not authored bot navigation.

## Verification

- Fresh final native build: `.codex-research/move-build-human-route-20260917-b`.
  Source and binary hashes are in its `20260917T230009711368Z.sources.json`.
- Fourteen report tests pass with `python -B -m unittest discover -s tools
  -p test_human_route_report.py`.
- Real client lifecycle smoke:
  `.codex-research/human-route-20260917T230054873654Z`.
  Three unique captures close as `manual_stop`, `enemy_flag_pickup`, and
  `disconnect`. The pickup capture has 83 rows, elapsed 675 ms, stationary
  native root, real final flag possession, and observed peak speed 250.
  **This is a synthetic pickup test**, positioned near the enemy flag before
  arming. It is not a flag-to-flag run or human performance evidence.
  Its report is retained beside `1789686077-1-c0.csv`.
- Recorder-disabled final-build regression:
  `.codex-research/human-route-disabled-bench-20260917-b/result.json`.
  `red_out_L1_stand`, timescale 1, edges on, parity 256: pass, 21.100 s,
  rewrite ownership throughout, zero parity mismatches. This is one affected
  check, not a new full qualification campaign. It matches build A's check.
- Failed/partial development checks remain under the earlier `human-route-*`
  directories. A connection failure was resolved after moving scripted
  commands into a config; shorter waits lost commands; menu-suppressed inputs
  produced no motion; the first pickup fixture acquired the flag before
  arming. The final lifecycle script waits for entry, closes the menu, and
  positions the pickup fixture farther away.
- Test clients and servers were stopped. No practice server is left running.

The next evidence is the owner's actual route. Confirm the starting stand,
settings and Force observations, then compare geometry, speed and landings
against the bot's retained routes. No claim of a sub-15-second bot route,
search completeness, or automatic policy learning follows from this work.
