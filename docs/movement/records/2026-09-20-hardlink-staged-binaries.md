# Campaign-local hardlinks for staged binaries

Implemented in `tools/staged_binary.py`; this record supersedes the proposal.
This changes future staging only. It does not reclaim the existing estimated
83.7 GB of EXE/DLL copies or convert any retained world.

## Storage and integrity contract

The helper copies each distinct source binary into a campaign-local
`.staged-binaries/<filename>-<sha256>/` directory, verifies the copy, marks it
read-only, and links world binaries to that master. Build outputs are never
hardlinked: rebuilding a source build cannot alter an existing world.
Each master allows 1,000 world links plus its own directory entry, then a new
master is copied. Microsoft documents a limit of 1,023 additional links:
[CreateHardLink](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createhardlinka).

For research outputs, the campaign root is the first directory below
`.codex-research`. Separate subprocess runners below that root share the cache.
For outputs elsewhere, the conservative default is a cache beside the staged
binary; Python callers can supply `campaign_root` explicitly for shared storage.
A standalone research world therefore has its own cache, with no cross-world
saving. Separate campaigns are deliberately independent.

A persistent OS file lock serializes master selection, creation, link-count
checks, linking and journaling across processes. OS locks release on process
exit; their tiny files remain. Existing destinations are refused, and fallback
copies use exclusive creation. Expected unsupported-filesystem, cross-volume,
link-limit or permission failures fall back to a private copy with a warning.
Unexpected errors propagate. Every master is hash-checked before reuse and
every staged destination is hash-checked before returning.

`staging.jsonl` records source, destination, master, SHA-256, UTC timestamp,
link/copy mode and any fallback reason. Failed partial masters are retained and
fail closed on later attempts; the helper never deletes or overwrites them.
Read-only attributes are an accident guard, not a security boundary: deliberately
changing a master or any linked world binary can still affect its peers. Do not
patch staged binaries. Existing runner source/binary binding checks remain.

## Integration

Seventeen binary copy calls in thirteen scripts now use the shared helper:

- `human_route_record.py`, `kejim_defense_play.py`
- `movement_bench.py`, `movement_explore.py`
- `route_approve.py`, `route_ownership_probe.py`
- `strafe_continuous_runner.py`, `strafe_cost_runner.py`
- `strafe_fixture_runner.py`, `strafe_flag_runner.py`
- `strafe_flight_replay.py`, `strafe_map_native_runner.py`, `strafe_play_smoke.py`

The former count of 23 included scripts that only mentioned binary hashes
while copying other files. Source snapshots, configs, recordings and results
continue to use their existing copy behavior. Explicit helper provenance and
dependency snapshot lists now include `staged_binary.py` where applicable.

## Accounting and retention

For 1,560 worlds using the same EXE/DLL pair, each binary needs two masters:
about 6.8 MB total for the pair instead of about 5.3 GB, plus directory/journal
metadata. This prevents future duplication; it is not a measured recovery of
existing storage. Backup tools that expand links can recreate all logical bytes.

Measure allocation once per unique volume/file identity, not once per path.
Summing per-path allocated sizes still double-counts hardlinks. On Windows,
[GetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle)
provides the volume serial and file index for this purpose.

Archiving a world on the same volume preserves its links; it does not release
the shared data. Data remain while any master, world or archived link exists.
The repository archive-only cleanup policy is unchanged. No existing worlds,
installed files, or prior symbol archives were converted or deleted.

## Validation

- Seven filesystem tests pass in `tools/test_staged_binary.py`: real link and
  hash identity, isolation from later build changes, rollover, four-process
  concurrent staging, reported copy fallback, existing/racing destination
  refusal, and corrupted-master refusal. Fixtures are retained under
  `.codex-research/test-staged-binary-20260921T014057Z-d9fabf41`.
- All 57 affected movement benchmark/build-binding and continuous/cost runner
  regression tests pass. Temporary-fixture deletion was disabled for this run;
  fixtures remain under `.codex-research/staging-runner-tests-f68f14d4`.
- AST parsing passes for the thirteen consumers; diff whitespace checks pass.
- Native startup smoke retained at
  `.codex-research/hardlink-native-smoke-c8852dc2`: one copied and two linked
  worlds loaded `jampgamex86.dll`, initialized `mp/ffa3`, shut down normally
  and exited zero with identical EXE/DLL hashes. This reused the retained
  `select-override-build-20260920-a` binaries solely for a loader smoke, not
  current-source movement qualification. `smoke.json` and console logs retain
  the results. For the two linked worlds plus their masters, six binary paths
  resolve to two file identities: 10,811,904 logical bytes and 3,603,968 uniquely
  allocated bytes (`storage.json`).

These checks validate staging behavior, not movement performance or a new
native qualification. No C/C++ sources or installed game files were changed by
this implementation.
