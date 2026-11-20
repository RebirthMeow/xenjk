# Rollout replay: exact reuse of repeated rollout prefixes

Recorded 2026-09-22. The owner directed the next step at the rollouts. By the
search-speed record's estimate, about 31 percent of rollout steps repeat
earlier work: shared first-hop prefixes plus the reroute pass re-flying
flights. This step reuses those steps exactly.

Worlds are under `.codex-research/replay-20260922-{a,b,c}/`, on builds
`row5-build-20260922-q`, `-r` and `-s`. Identity is checked against
`budget-default-20260922-a` (build p, budget 4, replay off).

## Mechanism (`bot_forecastReplay`: 1 replay, 2 verify, 0 off; default 0 when measured, 1 since the ruling at the end)

Every rollout records which frame buffer it wrote, together with its guard
scope, client, root (compared by value), clock and settings.

A new rollout inside the same scope starts with every other buffer whose
record matches. At each step it keeps the buffers that satisfy all three:

- their recorded state before the step equals this rollout's state, after
  the Force update, and so does the carried animation;
- they issued the same command as this rollout's own policy callback;
- the step is not the recorded rollout's last.

If any buffer survives, the rollout copies that buffer's Pmove output and
after-state instead of calling Pmove. Once none survives, every remaining step
is simulated as before.

The policy callback runs on every step with the rollout's own policy and
context, and writes that policy into the frame. Policies may differ between
siblings, because Pmove never reads them.

Why this is exact:

- After every rollout the scope asserts the rollout's own slot (entity and
  client) and the level struct unchanged; the whole entity array is compared
  once, at the scope's end. Pmove writes no other entity, so a record's
  `clean` bit means slot-and-level clean.
- The slot copy is rebuilt from the live entity at every start.
- Pmove is deterministic in the state, the command, the pmove settings and
  the world, except for its draws on the global random stream. The only one
  a `PM_NORMAL` client reaches is the in-air stir of a spinning saber
  animation, so a step with that animation before or after it is never
  replayed (added after review; the fixture never reached it). The splash
  draws are vehicle-only, and `PM_irand_timesync` is seeded by the command's
  server time, which the match compares.
- A rollout stops on the step that set an unsupported flag, so every earlier
  recorded step left the flags and guards clean.

Verify mode also calls Pmove on every replayable step. It counts any step
whose after-state, output, settings, slot, trace flags or animation guard
disagree with the buffer.

`RunCandidate` gives each candidate rollout the next slot of a 12-slot ring
(`rolloutRing`, about 12 MB of BSS; a frame is 3,772 bytes). The 2-hop and
1-hop candidates that share an aim and run-up are six rollouts apart, so
without the ring the sibling would already be overwritten. Every other reader
of `parity.frames` still reads the rollout it just ran. The ring is
unconditional; replay is off by default.

## Results

**Exactness.** There were 0 mismatches in 24,819 verified steps:

| Build | Verified steps | Mismatches |
| --- | ---: | ---: |
| q | 1,322 | 0 |
| r | 5,408 | 0 |
| s | 18,089 | 0 |

**Identity on build s.** Both modes are decision-identical to build p on all
six cells:

| Cell | Time (ms) | Commits |
| --- | ---: | ---: |
| `red_out_L1_stand`, class 75 | 15,950 | 11 |
| `red_out_L1_stand`, class 100 | 16,725 | 11 |
| `red_out_L1_stand`, class 125 | 15,925 | 11 |
| `red_out_L1_stand`, class 150 | 15,975 | 11 |
| `red_ret_L1_stand` | 19,075 | 13 |
| `blue_out_L1_stand` | 20,400 | 13 |

- Replay mode is compared with `traces` dropped, since skipped steps make
  fewer collision traces. The counter is volatile.
- Verify mode's `red_ret` shows the known 32,469 / 29,940 `traces` pair.
- Retries were missed pinned starts.

**The match rule mattered more than the mechanism.** Share of steps replayed:

| Build | Match rule | Steps replayed |
| --- | --- | ---: |
| q | policy state must match too | 3% |
| r | Pmove inputs only, one buffer | 6-8% |
| s | Pmove inputs only, 12-slot ring | 17-22% |

**Cost, one bot, budget 4.** The paired worlds make the same decisions: 41,877
steps and 569 rollouts.

| Arm | Rollout (ms) | Pmove (ms) | Forecast (ms) | p99 (ms) |
| --- | ---: | ---: | ---: | ---: |
| off (`m0-2`) | 541 | 327 | 633 | 11.0 |
| replay (`m1-1`, `m1-2`) | 507, 503 | 284, 282 | 592, 586 | 9.6, 10.8 |

Replay cuts rollout time by about 7 percent and Pmove by about 13 percent,
with 7,230 steps (17 percent) replayed. The frame tail does not measurably
move. `m0-2` also carries one 522 ms frame that has nothing to do with the
forecast.

## Reading

Replay is exact and the gain is small. There are two reasons:

- The prefixes that replay are mostly cheap airborne steps.
- The policy callback, about 40 percent of a step, must still run on every
  step, because only its command can confirm that the prefix holds.

Skipping the callback too would need a proof that siblings issue identical
commands until a known step. That proof would live in executor semantics, and
compilation cannot check it.

The switch stays off. Promoting it is the owner's call: 7 percent of rollout
time against the ring's 12 MB and the added code.

**Next rollout levers**, ordered by what the profile says is left:

1. The steering policy: about 5.5 us of a 13.7 us step. The corridor checks
   are 1.7 us of that, and the rest is the steering search.
2. Pmove itself: about 7 us, of which traces are about 2.6 us and the second
   ground trace is the largest phase.
3. Fewer rollouts per search. This changes decisions, so it is not exact.

## Promoted, and the engine made large-address-aware (owner, 2026-09-22)

**Owner ruling.**
- `bot_forecastReplay` defaults to 1: unset replays, and 0 turns it off.
- `codemp/CMakeLists.txt` links the client and dedicated engines with
  `/LARGEADDRESSAWARE` for 32-bit MSVC builds only. That gives 4 GB of address
  space on 64-bit Windows.

**Headroom.** The game DLL's image is 137.3 MB, 135.6 MB of it static data;
the ring added 10.2 MB. The running server's working set is about 85 MB,
because most static arrays are sized for the worst case and never touched.

**Build `row5-build-20260922-t`** (`.codex-research/replay-default-20260922-a/`).
The engine's PE header carries the LAA flag. With both cvars unset:
- the six cells are decision-identical to build p, with `traces` treated as
  volatile;
- the floor passes 36/36.

**Cost pair** (same decisions, 41,877 steps):

| Arm | Rollout (ms) | Pmove (ms) | p99 (ms) |
| --- | ---: | ---: | ---: |
| off | 525 | 317 | 9.6 |
| default (replay on) | 469 | 269 | 9.1 |

7,230 steps are replayed. The unpaired worlds read 509 against 488 ms.
