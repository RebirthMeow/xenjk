# Native predictor failure handling — September 6, 2026

The [adverse/runtime experiment](STRAFE_MOVEMENT_ADVERSE_RUNTIME_2026-09-06.md)
found exact guarded-oracle agreement in all six positive runtime episodes,
but its clone-only mismatch control failed the zero-live-step requirement.
All eight invalid candidate scores tied, so full selection chose candidate 0;
the later selected-forecast failure was handled after one authoritative Pmove.
That failed capture remains under the adverse campaign's `runtime-negative`.

The correction latches an oracle disagreement as a fixture failure, closes
the current candidate capture and aborts selection. Full selection ranks only
valid forecasts. Any selected or repair-forecast failure now stops at the
actual movement boundary before authoritative Pmove. The positive policy,
candidate table, native physics and empirical envelope rule are unchanged.

Use a fresh source-bound build after this change and the independent map-query
revision. Declare these native regressions before collection:

1. One nominal full-search reference at origin [0,0,24.125], velocity zero,
   40 Hz and 160 live frames, with runtime instrumentation disabled.
2. Derive a calibration payload only from that raw-audited reference's selected
   useful candidate. Its terminal envelope is the exact observed 12th state.
   Retain the eight-cell ABI, populate only the first cell, and use only the
   identical zero root. This is an ABI/predictor regression, not a replacement
   region bake or a claim about any unseen state.
3. Five serialized runtime coverage episodes at that exact root; one serialized
   full-search runtime episode. Require the existing strict native-oracle,
   full live parity and useful-outcome gates; report the two horizons separately.
4. Full-search and coverage clone-only mismatch controls. Each must record
   exactly one failed runtime candidate (160 or 12 steps), no selected forecast,
   no selection, restored world/global state, a detected PS mismatch, and zero
   raw live movement records. The native reason must be
   `runtime_oracle_mismatch`.
5. Retain nominal +100 Y airborne-impulse continuation reuse and a fresh
   non-runtime coverage episode at the exact calibration root. Their existing
   useful-exit/parity/actual-state replacement gates remain in force.

`tools/strafe_runtime_regression.py` builds the exact-root payload from the
new reference and independently reads the negative raw capture. Original
region evidence remains bound to its original build; no old build hashes are
relabelled. Time benchmarks run alone, and failed evidence is retained.

## Fresh-build result

Build `.codex-research/strafe-build-native-core-projection-20260906` passes the
declared checks. Evidence is under
`.codex-research/strafe-native-core-regression-20260906`; `summary.json` records
all eleven passing cases and 1,440 live steps. Both mismatch controls preserve
exactly one failed candidate (160 or 12 frames), zero live records, no selected
forecast, restored state and the required native reason. The impulse-reuse
case finishes at 2,975 ms with one replacement and two subsequent hits.

Five serialized exact-root coverage predictor times are 1,781.1, 1,757.2,
1,906.2, 1,649.7 and 1,807.8 us (median 1,781.1). The selected 160-frame
diagnostic median is 4,809.8 us. These costs include scratch setup and retained
trace rows; no complete live-controller or M3 affordability claim follows.
This calibration payload selects candidate 7 at the exact zero root and is
not the adverse campaign's clearance-ranked region payload.

The first corrected full-search negative stopped correctly in native code,
but its report generator tried to read the seven deliberately unattempted
candidate files. That failed reporting run is retained. The runner now skips
the full-success evaluator for declared oracle controls and retains I/O
failures as report errors. Both controls were rerun with the same C build and
payload. A separate audit normalization fix converts decoded vector tuples
to their JSON list form before comparing roots; no physical gate changed.
`qualification.json` and `qualification-v2.json` retain both helper bindings.
The original protocol bytes are saved alongside the results under this
document's filename, matching the pre-collection hash.
