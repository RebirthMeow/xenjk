# Both gates re-taken on the current defaults

Recorded 2026-09-26 (collection 00:12 to 00:44, 32 minutes). Owner order
2026-09-25: re-take the gates now that item pickups, planned drops and arrival
safety are default on (the re-take had been waived when each went on).

| Item | Value |
| --- | --- |
| Build | `deferpin-build-20260925-b` (sources = `arrivesafe-build-20260925-g` plus the deferred class pin; uncommitted on `movement/level1-gate`) |
| Defaults under test | `bot_forecastItems`, `bot_moveDropPlan`, `bot_moveArriveSafe`, `bot_moveWalkKeepPath` on, with everything the previous gate had |
| Graph | research seed `g1-98eb1e97c24a4e21` |
| Library | `.codex-research/slowdrop-chain-20260924-a/library/mp_ctf_kejim_-1301924053`, **not re-explored** |
| Settings | `bot_moveExitSpeed=500 bot_moveStatePrefer=1 bot_moveReroute=1`, everything else default |
| Worlds | `.codex-research/gate-20260926-a/` (the `gate-20260924-f` harness rebound to this build; `chain.sh`, `analyse.out.txt`, `progress.jsonl`) |
| Sources | all 1,125 hashes and both binaries matched before the first world and after the last (`verify-before.json`, `verify-after.json`) |

Pinned worlds used the deferred class pin
([record](2026-09-25-deferred-pin.md)): no pin retries in the whole campaign
(the previous gate retried 12 worlds).

## Level-2 gate

| Row | Result |
| --- | --- |
| 0. Pre-gate floors | Kejim 36/36 (phase B, library on), ctf4 36/36, Dantooine v3 36/36; zero deaths. **Met** |
| 1. Safety | Kejim floor as above; harness-on arm 354-427 comparisons per world, **zero mismatches**, times equal to the harness-off worlds. **Met** |
| 2. Speed | `red_out_L2_stand` **8,150 / 9,100 / 9,875 / 9,575 ms**, three worlds a class, identical; the same as the previous gate. **Met** |
| 3. Coverage | Twelve level-2 cases, three repetitions: **36 of 36, no death**. Medians 8,700 (blue return respawn5) to 11,400 (red out stand); tail 12,075. Previous gate: medians 7,625-11,825, tail 12,425 |
| 4. Cost | One bot, budget 4: p99 **13.6 / 13.4 ms** (previous 12.7 / 12.8), 4 and 2 frames over 25 ms per 800; worst 91.7 / 90.8 ms. **Met** |

## Level-1 gate

| Row | Result |
| --- | --- |
| 1. Safety | Floor **36/36**, zero deaths, owners `move` and `jumppad`; seeded regressions at class 125 pass (17,000 and 17,000, as before); respawn4 at class 50 pass (16,450 and 16,450; previous 15,525). Harness-on arm 490-745 comparisons per world, **zero mismatches**; decisions identical to the harness-off worlds in every field but `traces` (three pairs, as in the previous gate). **Met** |
| 2. Speed | Library **11,650 / 14,925 / 14,325 / 14,125 ms**, six worlds a class, identical; the same as the previous gate. Median **14,225** (bound 16,500), tail **14,925** (bound 18,000). **Met.** Controls 11,650 / 15,925 / 14,325 / 16,175 (class 150 was 16,525) |
| 3. Mechanism | Waiting share, library worst against control worst: 12.0 / 15.4, 32.1 / 48.1, 51.0 / 51.0, 28.1 / 38.7. **Met at every class, strictly** (the previous two gates needed an owner ruling at one class) |
| 4. Coverage | Twelve level-1 cases, three repetitions, library on and off: **72 of 72, no death**. The library selects only on `red_out_L1_stand` (`exact`, `phase`); elsewhere the arms differ only by unpinned start class |
| 5. Cost | As level 2. **Met** |

**Every row of both gates met.**

## Notes

- The library still fits: re-exploring it under the new defaults was not
  needed for the gates. Level-1 and level-2 stand times are unchanged to the
  millisecond at every class.
- Level-1 respawn4 at class 50 is 925 ms slower than the previous gate (both
  cases, identical). The run diverges at 7.0 s, where the new build installs a
  reroute the old one did not; at 10.3 s it loses speed at a turn (667 to 181
  u/s). Route divergence of the kind in the
  [drop-plan follow-up](2026-09-25-drop-plan.md#follow-up-2026-09-26-the-walk-priced-route-left-is-not-the-loss);
  not in any gate row, not traced further.
- Cost p99 rose about 0.7 ms against the previous gate, inside the frame.
