# The tape library: the bot selects its own decision tape, and the fallback is measured

Recorded 2026-09-22. Stage 3 of the
[decision-explorer work order](../../archive/movement-2026-10-05/PLAN.md#next-work-order-2026-09-22-the-decision-explorer),
which names two conditions before an explorer time can count toward the
[level-1 gate](../../archive/movement-2026-10-05/PLAN.md#level-1-exit-gate-2026-09-21): the bot selects the
tape itself, from the map, the leg, the start cell and its own phase, with the
harness setting nothing but the case; and the fallback is measured, with a
refused or absent entry returning the trip to the ranking and the trip still
completing.

> **Both conditions hold.** With `bot_moveTapeLibrary=1`, no tape named by the
> bench and a two-key library on disk, three fresh class-75 worlds and one
> class-125 world each form their own key, select their own tape **on the exact
> rung**, and complete `red_out_L1_stand` in **15,950 ms** and **15,925 ms**
> with **11 of 11 entries hit, no refused and no absent entry**, zero deaths
> and the movement owner on every frame.
>
> **The fallback is measured and it completes.** At classes 100 and 150 no key
> matches the phase, the ladder falls to the nearest phase on the same leg and
> cell, and the tape the bot then runs is mostly not admissible from that
> start: **2 of 11 hit at class 100** and **1 of 11 at class 150**, with 1 and
> 6 absent entries. Both trips complete under the ranking after the fallback -
> **19,150 ms against the 19,375 ms control and 17,025 against 17,575** - zero
> deaths, movement owner throughout, zero parity mismatches.
>
> **The new code is inert when it is off.** On the same build, with both new
> cvars unset, the class-125 control cell returns **15,925 ms with 13 commits,
> 5 of them state**, and the class-75 control returns **21,125 ms with 15 and
> 7**: the cells of the decision-tape and class-75 records, to the frame.

## What was asked

The library on disk beside the graph cache, bound to map and checksum the same
way; one index read once per map load; a key of leg, start cell and phase
formed by the bot at a leg start; a four-rung selection ladder whose every rung
is recorded; one new default-off cvar with `bot_moveDecisionTape` keeping its
meaning as the single-file override that wins over it; the phase not chosen by
the bot in this step. Then a fresh build, the control cell first, inertness,
selection, fallback, parity, and a bounded look at why the parity sampler's
comparison count varies world to world.

## Binding

Two builds, both fresh, both with `strafe_fixture_build.py`:

| Build | manifest | sha256 | Used for |
| --- | --- | --- | --- |
| `.codex-research/tape-library-build-20260922-a` | `20260922T081309722595Z.sources.json` | `425ce0174fc3c7ee7751ea0bb7dcc07220bb1ae6e68367c901ffdf1aea09a695` | the first probe only, retained under `build-a-probe/` |
| `.codex-research/tape-library-build-20260922-b` | `20260922T081924845983Z.sources.json` | `663852e1ca365af4dfda76d7627a7d595ea079f7a3044c95a319426ab9b32a43` | **every world reported below** |

Build -b: `openjkded.x86.exe` sha256
`34b98e1fd2a3ca471dbff018713adb77a752659846e1299f7a2966d288b71455`,
`jampgamex86.dll` sha256
`22a2759b5e9d3b9165cd543b87c94e8400715110e4ba53ee46ce87cc731db973`. **All
1,117 source hashes matched the working tree before and after collection**, so
no C source moved while worlds ran.

Tapes, copied into the campaign and indexed from there:

| Tape | sha256 | From |
| --- | --- | --- |
| `c75-best.tape`, 11 entries | `401cdce9acfc3847cd50353b1c8ad1b55dd83c8132044782b3e331fa0a52e066` | `.codex-research/decision-explorer-c75-20260922-a/tapes/` |
| `c125-self.tape`, 11 entries | `f51fa0f15cf40d3fee6f12195102abda25a78ee4893a87e69d9e31aa9412e459` | `.codex-research/decision-tape-verify-20260922-g/tapes/` |

Library `.codex-research/tape-library-20260922-a/library/mp_ctf_kejim_-1301924053/`,
`index.txt` sha256 `242fe9b1ea3c4fb9ed8893034d4e137ee2c4934a465e499d982a5dfc8365e331`:

    decisionindex 1 map=mp/ctf_kejim checksum=-1301924053 keys=2
    key t1e83 0 75 c75-best.tape
    key t1e83 0 125 c125-self.tape

Worlds under `.codex-research/tape-library-20260922-a/`, every one retained,
one world per bench call, sequential, each phase launched detached with its own
progress file (`run_library.py`, `run_phase.sh`, `progress.jsonl`,
`probe|select|fallback.out.txt`). Every world runs the decision-tape record's
own [settings block](2026-09-22-decision-tape.md#the-settings-every-world-runs),
case `red_out_L1_stand`, the 2026-09-16 seeded graph, `--edges --parity 256`,
`--momentum-trace`, class pinned with `--corner-class` in the 3,000-5,500 ms
window. **The bench names no tape in any library world**: it stages the
directory and sets `bot_moveTapeLibrary=1`, and which tape runs is the bot's.

## The key the bot forms, and the one thing the design had wrong

The leg-id is the objective the leg runs to: the bot's team and the objective
entity it holds, `t<team>e<entity>`. The start cell is the cell the planner
would plan the leg from - `Graph_CellAt` at a step's tolerance, else
`Graph_Nearest` within 64 units - now one function, `Move_RootCell`, so the key
and the plan cannot disagree. The phase is `level.time` modulo 200 at the leg
start, and it is **not** chosen: nothing delays the bot's first command, so the
phase is whatever the world's ladder offers, which is what the class pin then
selects for.

**The first build got the leg-id wrong, and the probe said so.** The fixture's
release runs inside the rcon corner command, a server frame before the bot's
next think, and at that instant the runner holds **no objective entity at
all**: build -a's probe world printed
`[TAPE] select leg=t1e-1 cell=0 phase=75`. A key formed there names no
objective, so two different legs from one stand would collide on it. Build -b
keeps the leg start's own cell and phase and completes the key at that leg's
first think, in `Move_Plan`, before the leg's first search. The probe then
prints the key the design asked for:

    [TAPE] select leg=none  cell=0 phase=75 rung=pending ...      (the release)
    [TAPE] select leg=t1e83 cell=0 phase=75 rung=none    ...      (first think)

**`t1e83`, cell `0`, phase = the class.** Cell 0 is confirmed independently:
the first replan after the release reports `from=0`. The return leg the bot
forms for itself at the flag is a different key - `t1e82`, cell 45 or 192,
phase 0, 25 or 50 - and finds nothing in a two-key library, which is the
selection refusing rather than a tape riding on into the next leg.

Two engineering facts a reader needs. The release's own selection line is
printed inside the rcon command and therefore **never reaches `server.log`**;
it comes back in the corner reply, as the `[CORNER] START` line does. And the
phase at a class-pinned stand is the class by construction: the leg starts on
the pinned level, so `level.time % 200` is the pin.

## The ladder, and what each rung cost

`TapeSelect` sits beside `TapeLoad` and calls it. `TapeLoad`, `TapeEntryNear`,
`TapeMatches`, `TapeHop` and the selection hook after `StatePreferred` are
untouched; admission, physics, cadence, the ranking weights and the tape format
are unchanged, and there is one new cvar and no other.

1. the exact key: leg, cell and phase;
2. the same leg and cell at the nearest phase on the 200 ms ring;
3. a cell within one graph link of the start, same leg, nearest phase;
4. nothing: the tape is cleared and the leg runs under the ranking.

Every rung's candidate count is printed on the selection line and again on the
census totals line (`exact=`, `phase_keys=`, `link_keys=`, `keys=`), so a
fallback is read off the world rather than inferred. No world in this campaign
reached rung 3: at every start the leg and the cell matched and only the phase
did not, which is what a library built from two classes of one stand should do.

## The worlds

Eleven worlds, nine of them complete; the two that did not are pin misses of
the class ladder, retained and replaced under a suffixed name, not failures of
the trip. Zero deaths in every world and the movement owner on every frame of
every completed one.

| World | Class | Library | Release | Completed | Commits / state | Rung | Tape | Census | Parity compared / mismatches |
| --- | ---: | --- | ---: | ---: | ---: | --- | --- | --- | ---: |
| `c125-control` | 125 | off | 3125 | **15,925 ms** | 13 / 5 | - | - | - | 687 / 0 |
| `c75-control` | 75 | off | 3875 | **21,125 ms** | 15 / 7 | - | - | - | **0** / 0 |
| `probe-c75` | 75 | probe | - | pin miss | - | - | - | - | - |
| `probe-c75-a2` | 75 | probe | 3275 | 21,125 ms | 15 / 7 | none | - | - | 885 / 0 |
| `c75-lib-1` | 75 | library | 3875 | **15,950 ms** | 13 / 2 | **exact** | `c75-best` | **11 hit, 0 refused, 0 absent**, deferred 1 | 718 / 0 |
| `c75-lib-2` | 75 | library | 3875 | **15,950 ms** | 14 / 2 | **exact** | `c75-best` | **11 hit, 0 refused, 0 absent**, deferred 1 | 718 / 0 |
| `c75-lib-3` | 75 | library | 3875 | **15,950 ms** | 14 / 2 | **exact** | `c75-best` | **11 hit, 0 refused, 0 absent**, deferred 1 | 698 / 0 |
| `c125-lib-1` | 125 | library | - | pin miss | - | - | - | - | - |
| `c125-lib-1-a2` | 125 | library | 4125 | **15,925 ms** | 13 / 5 | **exact** | `c125-self` | **11 hit, 0 refused, 0 absent**, deferred 2 | 727 / 0 |
| `c100-lib-1` | 100 | library | 3100 | **19,150 ms** | 14 / 4 | phase | `c75-best` | 2 hit, 0 refused, 1 absent, deferred 4 | 816 / 0 |
| `c150-lib-1` | 150 | library | 3350 | **17,025 ms** | 13 / 6 | phase | `c125-self` | 1 hit, 0 refused, 6 absent, deferred 3 | 741 / 0 |

### 1. Inertness

`c75-control` runs with `bot_moveTapeLibrary=0` and no tape and returns
**21,125 ms with 15 commits, 7 of them state** - the class-75 record's control
cell, commit for commit. `c125-control` runs with both new cvars unset and
returns **15,925 ms, 13 and 5**, the decision-tape record's control cell, which
is the cell the work order requires before a rebuild may be used. The
`TapeSync` gate is the only thing the library added to a frame, and with the
cvar clear that gate is not reached.

### 2. Selection

Each of the four library worlds prints its own key and the rung that answered
it, on the selection line and again on the census totals line:

    census entries=11 hits=11 refused=0 absent=0 deferred=1 cursor=11
      leg=t1e83 cell=0 phase=75 rung=exact
      file=mp_ctf_kejim_-1301924053/c75-best.tape exact=1 phase_keys=1 link_keys=0 keys=2

The class-75 worlds reproduce the class-75 record's reproduction to the field:
**every aim gap 0.0000**, `attempts=1` on ten entries and `2` on entry 1,
`matches` of 6, 4, 1, 1, 3, 6, 3, 1, 1, 2, 1, the last entry taken with
`why=arrives`, `deferred=1`. The class-125 world reproduces the self-control's
line including `deferred=2`. **The only difference from those campaigns is who
chose the tape**, and the trips are identical.

The whole-world commit counts differ between `c75-lib-1` (13 straight, 2 state)
and the other two (14 and 2), as they did in the class-75 record: the leg is 11
decisions in all three and the extra commit is on the return after the flag.

### 3. Fallback

Neither class 100 nor class 150 has a key. The ladder falls to rung 2, the
nearest phase on the same leg and cell, and the tie at class 100 - 25 ms from
both keys - is broken by index order, which is deterministic and arbitrary and
is reported as such.

| World | Nearest key | Tape run | Hits | Refused | Absent | Deferred | Completed | Known control | Change |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `c100-lib-1` | 75, tie with 125 | `c75-best` | 2 of 11 | 0 | 1 | 4 | **19,150 ms** | 19,375 ms | **-225** |
| `c150-lib-1` | 125 (25 against 75) | `c125-self` | 1 of 11 | 0 | 6 | 3 | **17,025 ms** | 17,575 ms | **-550** |

**Both trips complete, with zero deaths, the movement owner on every frame and
zero parity mismatches over 816 and 741 comparisons.** Class 100 hits entries 0
and 1 and then entry 2 is absent with the nearest candidate 0.0441 degrees
away; class 150 hits entry 0 and then entry 1 is absent at 0.0923 degrees.
After the first miss the bot goes on under the ranking, and the entries it
later passes the roots of are answered as it goes - `unseen` where the search
committed nothing at all, which the cursor holds for rather than consuming.

`c150-lib-1`'s **17,025 ms is the transfer collection's own class-150 tape
figure**, from three worlds in which the bench named the tape
([decision tape](2026-09-22-decision-tape.md#the-transfer-collection)). The
same trip arrived at by the bot's own selection is the cross-check that the
library changed who chose and nothing else.

### 4. Parity, and the world that reports no comparisons

Zero mismatches in every completed world, over 687 to 885 comparisons - except
`c75-control`, which reports **zero comparisons and `horizons=0`**, exactly as
one of three identical class-75 controls did in the class-75 record. That is
now **twice, on two builds**, so it is reproducible and worth a cause.

**The cause is the tally's own print condition, not the sampler's coverage.**
`Bot2_MoveParityAfterPmove` increments `parity.compared` on every matched frame
and prints the `reason=horizon` event only when

    parity.index>=parity.count && (completion==HORIZON || truncated) &&
    (parity.rollouts%10==0 || parity.planned)

`parity.rollouts` counts every candidate rollout of every search, so
`rollouts%10==0` is a lottery on an arbitrary counter, and `planned` is true
only for a horizon built while a committed sequence was in flight. The events
that did print say so: 1,100 rollouts in `c75-control-2`, 80 and 110 in
`c75-control-3`, all `planned=0`; `c75-tape-1`'s single event has
`planned=1` at 553. And `tools/movement_bench_lib.parity_summary` reports
`compared` as the largest `frames=` it saw on such an event, so a world that
never printed one reports zero however much it compared.

**The comparisons happened.** Both zero-comparison worlds carry
`parity_armed=1` with `parity_index` up to **52** on their own `onward`
completion lines - the coverage detail the composition gate reads - so at least
that many frames were compared and matched in a world the summary calls zero.

**The repair is not small, so it is reported and left.** Printing on every
exhausted horizon multiplies a per-frame-path print, which this project has
measured moving a trip by 2,000 ms once already. The safe repair is one final
tally per leg, emitted where the tape census is emitted - and that needs a new
parity reason, which `movement_bench_lib.verdict` currently fails a world for
and `parity_summary` currently counts as a mismatch. That is three coupled
changes to the pass/fail semantics of every retained world, on a build that
must first reproduce two control cells. It belongs in its own step, with the
two analysis functions changed in the same commit.

## What failed

- **The leg-id, on the first build.** Named above; build -a's three probe
  worlds are retained under `build-a-probe/` and its control cell is the same
  15,925 / 13 / 5, so the rebuild changed the key and nothing else.
- **`movement_decision_explore.census` could not read the new totals line.**
  The totals line now carries the selection - `leg=t1e83`, `rung=exact`, the
  file - and the reader parsed every field as an integer. It raised *after*
  `c75-lib-1` had run and passed, so the world is intact; the reader was
  repaired (a field that is not a count is kept as printed, with a
  regression), `c75-lib-1`'s progress row was written from its retained world
  with the repaired reader and carries a note saying so, and the remaining
  worlds ran under the repaired tool. No C source moved.
- **Two pin misses.** `probe-c75` and `c125-lib-1` exhausted four worlds each
  without their class: a world offers only the four classes congruent to its
  own opening level modulo 50, which stage 1 established. Both are retained
  and were replaced under `-a2`. One of `c125-lib-1`'s own miss worlds landed
  on class 100 and is the only other place rung 2 is seen.

## What this does not establish

The library is two keys of one leg on one map for one case. That the exact
rung reproduces says a tape indexed at the key a world forms is found and run;
it says nothing about a library large enough for the keys to compete, and
nothing about rung 3, which no world here reached.

**The fallback times are one world each and are not a result about the
fallback.** 19,150 against a 19,375 ms control and 17,025 against 17,575 are
single worlds against figures collected elsewhere; the class-150 pair agrees
with the transfer collection's three worlds, and the class-100 figure does not
have that support. A rung-2 selection is a tape from a different start class
and the transfer question already answered what that is worth: mostly absent
entries, and the gain is the reachable prefix's, not the tape's.

The phase is still the world's, not the bot's. The work order leaves choosing
it - delaying the first command by up to 175 ms to select the class - as
something to measure at stage 3, and nothing here measures it: every world
took the phase its ladder offered and the harness pinned the class.

No gate row is reported moved by this record. What it establishes is that
the two conditions stage 3 names are met, which is the question it was asked.
