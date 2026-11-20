# The four-class library: row 2's median is met, its tail is not, and two tapes do not reproduce

Recorded 2026-09-22. The first measurement of
[gate](../../archive/movement-2026-10-05/PLAN.md#level-1-exit-gate-2026-09-21) rows 2 and 3 with **the bot
choosing its own decision tape at every start class**, on the build the
[tape library](2026-09-22-tape-library.md) landed on, with the four explorer
campaigns' tapes indexed under one key each.

> **Row 2's median is met and its tail is not.** Over the four declared start
> classes weighted equally, the library arm's median is **16,412 ms** against
> the row's 16,500, and its tail - the worst completed library world - is
> **18,675 ms** against the row's 18,000. The control arm on the same worlds is
> **18,475 ms** and 21,125. Row 2 is **not met**.
>
> **The declared-weight mean did not move at all**: 17,438 ms library against
> 17,428 ms control, because class 150 carries weight 47 and the library made
> it **1,100 ms slower** than its own control.
>
> **Two of the four tapes do not reproduce.** Every one of the twelve library
> worlds selected on the **exact** rung, so selection is not at fault. The
> class-75 and class-125 tapes hit **11 of 11** entries with every aim gap
> 0.0000 and return 15,950 and 15,925 ms. The class-100 tape hits **4 of 11**
> (3 absent, 4 unseen) and the class-150 tape **2 of 10** (5 absent, 3 unseen);
> those two tapes were extracted from their explorer's best branch world and
> **were never run on a fresh world before this campaign**. Their trips are
> 16,875 and 18,675 ms against the explorer's own 16,725 and 15,975.
>
> **Row 3 is not met at any class**, library or control. The library arm's
> per-class waiting shares are **30.4, 58.3, 37.9 and 62.5 percent** against a
> threshold of 35, and an `uncommitted_grounded` run exists at classes 100 and
> 150. No candidate was `admitted` at any waiting frame in any of the 24
> worlds.
>
> **Row 1 signal**: 22 of 24 worlds report a non-zero parity comparison count;
> `c75-ctl-1` and `c75-ctl-3` report **zero**. Zero mismatches everywhere, zero
> deaths, the movement owner on every frame of every completed world. Nothing
> was repaired here.

## What was asked

Index the class-75, 100, 150 and 125 tapes into one library under the leg
`t1e83`, start cell 0 and their own phases, bound to their source campaigns by
hash; then measure rows 2 and 3 with the bot selecting, three library worlds
against three tape-off controls at each class, alternated, one world per bench
call, sequential, detached, with every world retained. Report row 2 exactly as
the gate states it, report row 3 as the plan's 2026-09-22 re-basing defines it,
and note the row-1 parity signal without repairing anything. No C source was to
move, and none did.

## Binding

Build `.codex-research/tape-library-build-20260922-b`, manifest
`20260922T081924845983Z.sources.json` sha256
`663852e1ca365af4dfda76d7627a7d595ea079f7a3044c95a319426ab9b32a43`,
`openjkded.x86.exe` sha256
`34b98e1fd2a3ca471dbff018713adb77a752659846e1299f7a2966d288b71455`,
`jampgamex86.dll` sha256
`22a2759b5e9d3b9165cd543b87c94e8400715110e4ba53ee46ce87cc731db973`. **All
1,117 source hashes matched the working tree before the first world and again
after the last** (`verify_sources.py`, `manifest-before.json`,
`manifest-after.json`), so no C source moved while worlds ran. No rebuild.

The four tapes, each bound to the campaign that produced it
(`manifest-tapes.json`):

| Phase | Tape | sha256 | From | That campaign's control / best |
| ---: | --- | --- | --- | --- |
| 75 | `c75-best.tape`, 11 entries | `401cdce9...a52e066` | `.codex-research/decision-explorer-c75-20260922-a`, 175 worlds, 163 completed, depth 3 | 21,125 / **15,950 ms** |
| 100 | `c100-best.tape`, 11 entries | `ca076e87...728e9d54` | `.codex-research/decision-explorer-c100-20260922-a`, 167 worlds, 150 completed, depth 3 | 19,375 / **16,725 ms** |
| 150 | `c150-best.tape`, 10 entries | `24ce31a5...18105ae4` | `.codex-research/decision-explorer-c150-20260922-a`, 151 worlds, 140 completed, depth 2 | 17,575 / **15,975 ms** |
| 125 | `c125-self.tape`, 11 entries | `f51fa0f1...9412e459` | `.codex-research/decision-tape-verify-20260922-g` | 15,925 / **15,925 ms** |

Two facts about that table a reader needs. The class-75 tape was produced on
build `decision-tape-build-20260922-f` and the other three on
`tape-library-build-20260922-b`; the class-75 tape has already been shown to
reproduce on `-b` ([tape library](2026-09-22-tape-library.md)). And the
class-125 explorer found **nothing better than the ranking's own trip in 44
worlds**, so its "best" is the control's own sequence: the retained class-125
tape is `c125-self.tape`, which differs from that campaign's `c125-best.tape`
only in the source comment line. The class-150 tape has ten entries, not
eleven, because its leg ended on an arriving hop whose decision no tape can
name (`tape_unnameable_level=18575`).

Library `.codex-research/tape-library-four-20260922-a/library/mp_ctf_kejim_-1301924053/`,
built with `tools/movement_tape_library.py`, `index.txt` sha256
`64b6e5ef112f22b97c6a76d4e577e38151beab1fbeab6376a02c7fa9b1a593cc`:

    decisionindex 1 map=mp/ctf_kejim checksum=-1301924053 keys=4
    key t1e83 0 75 c75-best.tape
    key t1e83 0 100 c100-best.tape
    key t1e83 0 150 c150-best.tape
    key t1e83 0 125 c125-self.tape

Worlds under `.codex-research/tape-library-four-20260922-a/`, every one
retained, one bench call each, sequential, one detached campaign
(`run_four.py`, `run_four.sh`, `progress.jsonl`, `campaign.out.txt`,
`campaign.done`). Every world runs the decision-tape record's own
[settings block](2026-09-22-decision-tape.md#the-settings-every-world-runs),
case `red_out_L1_stand`, the 2026-09-16 seeded graph, `--edges --parity 256`,
`--momentum-trace`, the class pinned with `--corner-class` in the 3,000-5,500
ms window. **The bench names no tape in any library world**: it stages the
directory and sets `bot_moveTapeLibrary=1`, and which tape runs is the bot's.
The control worlds are staged with no library at all.

## The worlds

Twenty-five worlds ran; 24 completed. `c100-ctl-3` exhausted its four level
attempts without landing class 100 - a pin miss of the class ladder, not a
failure of the trip - and is retained and replaced by `c100-ctl-3-a2`. Zero
deaths and the movement owner on every frame of every completed world.

| World | Class | Arm | Release | Completed | Commits / state | Rung | Tape | Census | Parity compared / mismatches |
| --- | ---: | --- | ---: | ---: | ---: | --- | --- | --- | ---: |
| `c75-lib-1` | 75 | library | 3875 | **15,950 ms** | 14 / 2 | **exact** | `c75-best` | **11 hit**, 0 refused, 0 absent, deferred 1 | 718 / 0 |
| `c75-ctl-1` | 75 | control | 3875 | 21,125 ms | 15 / 7 | - | - | - | **0** / 0 |
| `c75-lib-2` | 75 | library | 3875 | **15,950 ms** | 14 / 2 | **exact** | `c75-best` | **11 hit**, 0, 0, deferred 1 | 698 / 0 |
| `c75-ctl-2` | 75 | control | 3875 | 21,125 ms | 16 / 8 | - | - | - | 70 / 0 |
| `c75-lib-3` | 75 | library | 3875 | **15,950 ms** | 14 / 2 | **exact** | `c75-best` | **11 hit**, 0, 0, deferred 1 | 698 / 0 |
| `c75-ctl-3` | 75 | control | 3075 | 21,125 ms | 16 / 8 | - | - | - | **0** / 0 |
| `c100-lib-1` | 100 | library | 5100 | 16,875 ms | 13 / 4 | **exact** | `c100-best` | 4 hit, 0 refused, **3 absent**, 4 unseen, deferred 1 | 785 / 0 |
| `c100-ctl-1` | 100 | control | 3300 | 19,375 ms | 13 / 7 | - | - | - | 815 / 0 |
| `c100-lib-2` | 100 | library | 3100 | 16,875 ms | 16 / 6 | **exact** | `c100-best` | 4 hit, 0, **3 absent**, 4 unseen, deferred 1 | 705 / 0 |
| `c100-ctl-2` | 100 | control | 3100 | 19,375 ms | 13 / 7 | - | - | - | 805 / 0 |
| `c100-lib-3` | 100 | library | 4100 | 16,875 ms | 14 / 5 | **exact** | `c100-best` | 4 hit, 0, **3 absent**, 4 unseen, deferred 1 | 765 / 0 |
| `c100-ctl-3` | 100 | control | - | pin miss | - | - | - | - | - |
| `c100-ctl-3-a2` | 100 | control | 4300 | 19,375 ms | 13 / 7 | - | - | - | 855 / 0 |
| `c150-lib-1` | 150 | library | 3350 | **18,675 ms** | 16 / 5 | **exact** | `c150-best` | 2 hit, 0, **5 absent**, 3 unseen, deferred 1 | 787 / 0 |
| `c150-ctl-1` | 150 | control | 3350 | 17,575 ms | 16 / 8 | - | - | - | 763 / 0 |
| `c150-lib-2` | 150 | library | 3350 | **18,675 ms** | 16 / 5 | **exact** | `c150-best` | 2 hit, 0, **5 absent**, 3 unseen, deferred 1 | 787 / 0 |
| `c150-ctl-2` | 150 | control | 3350 | 17,575 ms | 16 / 8 | - | - | - | 763 / 0 |
| `c150-lib-3` | 150 | library | 3550 | **18,675 ms** | 16 / 5 | **exact** | `c150-best` | 2 hit, 0, **5 absent**, 3 unseen, deferred 1 | 815 / 0 |
| `c150-ctl-3` | 150 | control | 3350 | 17,575 ms | 16 / 8 | - | - | - | 763 / 0 |
| `c125-lib-1` | 125 | library | 3125 | **15,925 ms** | 13 / 5 | **exact** | `c125-self` | **11 hit**, 0, 0, deferred 2 | 667 / 0 |
| `c125-ctl-1` | 125 | control | 3125 | 15,925 ms | 13 / 5 | - | - | - | 667 / 0 |
| `c125-lib-2` | 125 | library | 3125 | **15,925 ms** | 13 / 5 | **exact** | `c125-self` | **11 hit**, 0, 0, deferred 2 | 667 / 0 |
| `c125-ctl-2` | 125 | control | 3125 | 15,925 ms | 13 / 5 | - | - | - | 687 / 0 |
| `c125-lib-3` | 125 | library | 3125 | **15,925 ms** | 13 / 5 | **exact** | `c125-self` | **11 hit**, 0, 0, deferred 2 | 687 / 0 |
| `c125-ctl-3` | 125 | control | 3125 | 15,925 ms | 13 / 5 | - | - | - | 687 / 0 |

**Every library world selected on the exact rung**, with `exact=1` and `keys=4`
on its own census totals line, and named the tape its phase indexes. Selection
is not what failed anywhere in this campaign. The three completed times per
class are identical in all eight cells, across release levels up to 2,000 ms
apart at class 100, which is the determinism the classes already had.

**Two classes did not hit every entry.** The census says where each stopped:

| World | Entries hit | First miss | Aim gap at the miss | After the miss |
| --- | --- | --- | ---: | --- |
| `c100-lib-*` | 0, 1, 2, 3 | entry 4, level 9500, `absent` | 0.3110 | 5 and 6 absent, 7 to 10 `unseen` |
| `c150-lib-*` | 0, 1 | entry 2, level 7875, `absent` | 0.0581 | 3, 5, 8, 9 absent, 4, 6, 7 `unseen` |

An `absent` entry is the engine saying the decision the tape names was not
among that search's admitted candidates at that root, with the nearest
candidate's aim gap given; `unseen` is a root the search never committed
anything at, which the cursor holds for rather than consuming. Class 100's
entry 3 was a **hit with an aim gap of 0.4217**, not 0.0000, so the trip had
already been displaced when the first absent entry arrived. The class-75 and
class-125 worlds hit every entry with a gap of 0.0000 at every one.

## Row 2, as the gate states it

The gate asks for `red_out_L1_stand` median at or under 16,500 ms and tail
(worst completed repetition) at or under 18,000 ms, over the four declared
start classes **weighted equally**, at least six matched repetitions per class.
Computed here as the task defines the equal weighting: each class's time is its
three worlds' median, and the row's median is the median of those four
per-class times. The tail is the worst completed library world.

| Class | Library worlds | Library median | Control worlds | Control median | Change |
| ---: | --- | ---: | --- | ---: | ---: |
| 75 | 15,950 x3 | **15,950** | 21,125 x3 | 21,125 | **-5,175** |
| 100 | 16,875 x3 | **16,875** | 19,375 x3 | 19,375 | **-2,500** |
| 125 | 15,925 x3 | **15,925** | 15,925 x3 | 15,925 | 0 |
| 150 | 18,675 x3 | **18,675** | 17,575 x3 | 17,575 | **+1,100** |

Library: the four per-class times sorted are 15,925, 15,950, 16,875, 18,675, so
the equal-weight median is **(15,950 + 16,875) / 2 = 16,412.5 ms**, at or under
16,500. The tail is **18,675 ms**, above 18,000.

Control on the same worlds: 15,925, 17,575, 19,375, 21,125, median
**18,475 ms**, tail 21,125 ms.

| Row 2 | Library | Control | Gate |
| --- | ---: | ---: | ---: |
| Equal-weight median | **16,412.5 ms** | 18,475 ms | 16,500 - **met** |
| Tail (worst completed world) | **18,675 ms** | 21,125 ms | 18,000 - **not met** |

**Row 2 is not met**, on its tail. Three worlds per class is also half the six
matched repetitions the row asks for, so even the median is not yet reported
against the row's own repetition count.

**The declared-weight mean, for continuity with earlier records** (weights 4,
12, 30, 47 for classes 75, 100, 125, 150; not the gate's definition):
**17,438 ms library against 17,428 ms control**, a change of **+11 ms**. The
two arms are indistinguishable under that weighting because it puts 47 of 93
on class 150, the one class the library made worse. The last figure on the
gate row is 17,394 ms; this is not an improvement on it.

## Row 3, on the same worlds

`tools/movement_leg_budget.py` under LEG_BUDGET2, run per world over its own
retained case directory. **Waiting is `precommit_grounded` plus
`uncommitted_grounded` frames**, the two owners the plan's re-based row 3
names, over that leg's grounded frames. The other four owners
- `leg_start_stand`, `single_grounded_frame`, `runup_before_launch` and
`committed_grounded_without_runup` - are **not** counted as waiting and stay in
the denominator, which is how the 30.4 and 37.9 percent figures the plan quotes
were computed.

| World | Window | Frames | Grounded | Waiting | Share | `precommit` (runs) | `uncommitted` (runs) | Airborne |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `c75-lib-1..3` | 15,750 ms | 630 | 69 | 21 | **30.4 %** | 21 (1) | 0 (0) | 0.890 |
| `c75-ctl-1..3` | 21,000 ms | 840 | 198 | 141 | 71.2 % | 107 (6) | 34 (1) | 0.764 |
| `c100-lib-1..3` | 16,750 ms | 670 | 115 | 67 | **58.3 %** | 49 (3) | 18 (1) | 0.828 |
| `c100-ctl-1,2,3-a2` | 19,250 ms | 770 | 148 | 88 | 59.5 % | 65 (3) | 23 (2) | 0.808 |
| `c125-lib-1..3` | 15,750 ms | 630 | 66 | 25 | **37.9 %** | 25 (2) | 0 (0) | 0.895 |
| `c125-ctl-1..3` | 15,750 ms | 630 | 66 | 25 | 37.9 % | 25 (2) | 0 (0) | 0.895 |
| `c150-lib-1,2` | 18,500 ms | 740 | 128 | 80 | **62.5 %** | 69 (4) | 11 (1) | 0.827 |
| `c150-lib-3` | 18,550 ms | 742 | 130 | 82 | **63.1 %** | 69 (4) | 13 (1) | 0.825 |
| `c150-ctl-1..3` | 17,500 ms | 700 | 97 | 49 | 50.5 % | 49 (3) | 0 (0) | 0.861 |

The three worlds of each arm agree exactly except `c150-lib-3`, whose release
was 200 ms later and which is two frames longer.

| Row 3 | Library | Control | Gate |
| --- | ---: | ---: | ---: |
| Worst per-world share | **63.1 %** | 71.2 % | 35 % - **not met** |
| Pooled, all worlds | 581 of 1,136 = **51.1 %** | 909 of 1,527 = 59.5 % | - |
| Any `uncommitted_grounded` run | **yes** (classes 100 and 150) | yes (classes 75 and 100) | none - **not met** |

**Row 3 is not met at any class in either arm.** It is met nowhere near: the
best library class is class 75 at 30.4 percent, which is the only cell under
the threshold, and the class-125 pair is 37.9 percent in both arms. An
`uncommitted_grounded` run - a grounded run the leg ends without ever
committing from - exists in the class-100 and class-150 library worlds, and the
row excludes any.

**Row 3 moved with row 2, and in both directions.** Class 75 gained 5,175 ms
and its share fell 40.8 points; class 150 lost 1,100 ms and its share rose 12.0
points; class 125 changed in neither. Class 100 gained 2,500 ms with its share
essentially unchanged (58.3 against 59.5), which is the case the plan's rule
warns about: a speed gain whose mechanism the waiting share does not confirm.
On that class the gain is the airborne fraction's, 0.828 against 0.808, on a
window 2,500 ms shorter.

### The candidate-outcome census at waiting frames

As the tool records them, per world, with no inferred failed searches. The
three worlds of an arm are identical unless noted.

| Arm | `refused/trigger_contact_unknown` | `fell` | `corridor` | `no_onward` | `no_path` | `slower_route` | `slower/trigger_contact_unknown` | `admitted` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `c75-lib-*` | 42 | 21 | - | - | - | - | - | **0** |
| `c75-ctl-*` | 210 | 28 | - | 15 | 6 | - | - | **0** |
| `c100-lib-*` | 75 | 13 | 13 | 2 | - | - | 4 | **0** |
| `c100-ctl-*` | 169 | 32 | - | - | - | - | 4 | **0** |
| `c125-lib-*` | 50 | 15 | - | - | 3 | 6 | - | **0** |
| `c125-ctl-*` | 50 | 15 | - | - | 3 | 6 | - | **0** |
| `c150-lib-*` | 108 | 15 | 61 | - | 5 | - | - | **0** |
| `c150-ctl-*` | 114 | 20 | - | - | - | - | - | **0** |

Pooled: 825 refusals for a trigger contact the forecast cannot model of 1,299
judged candidates at library waiting frames (63.5 percent), and 1,629 of 2,016
in the control (80.8 percent). **Not one candidate was `admitted` at a waiting
frame in any of the 24 worlds**, which is what the owner label means and what
the leg budget and the class-75 record both found. The one new column is
`corridor`: 222 out-of-corridor candidates at library waiting frames, 61 per
world at class 150 and 13 at class 100, and none in any control. Those are the
two classes whose tape stopped hitting, and they are judged at frames where the
bot is off the trip its tape was built from.

## Row 1 signal

The row requires a non-zero parity comparison count per world. Per world, from
the bench's own parity summary:

- **22 of 24 completed worlds report a non-zero count**, 70 to 855 comparisons.
- **`c75-ctl-1` and `c75-ctl-3` report zero**, with zero mismatches. Both are
  class-75 controls, the same cell that reported zero in the class-75 record
  and again in the tape-library record. `c75-ctl-2`, the third world of that
  same cell, reports **70**.
- **Zero mismatches in all 24 worlds.**

That is now four zero-comparison worlds on three builds, all of them the
class-75 control cell. The [tape-library record](2026-09-22-tape-library.md)
established the cause - the tally's `rollouts % 10` print condition in
`Bot2_MoveParityAfterPmove`, not the sampler's coverage, with `parity_index` up
to 52 on the same worlds' own completion lines - and this campaign reproduces
it without touching it. **Nothing was repaired here**; the repair changes every
retained world's pass/fail semantics and is its own step.

## What failed

- **The class-100 and class-150 tapes do not reproduce.** They are the two of
  the four that were **never run on a fresh world before this campaign**: the
  class-75 tape was verified on three fresh worlds by its own record and again
  by the tape library, and the class-125 tape is a self-control that has
  reproduced twice. A branch world's completed time is the time of a world
  that ran under a prefix tape plus a forced index; the tape extracted from its
  leg names all eleven decisions, and replaying all eleven from the start is a
  different experiment. The explorer's stopping rule reads the branch world's
  time, so **an explorer result is not a tape result until the tape has run on
  its own**, and this campaign is the first time that was asked of these two.
- **Class 150 is 1,100 ms slower under its own tape than under the ranking.**
  Its tape hits 2 of 10 entries and then the bot finishes under the ranking
  from a displaced state, and the displacement costs more than the two hits
  bought. The library is not inert when its tape is wrong: a partly-hitting
  tape is worse than no tape at that class.
- **One pin miss.** `c100-ctl-3` exhausted four level attempts without landing
  class 100, which a world can do because it offers only the four classes
  congruent to its own opening level modulo 50. Retained, replaced under
  `-a2`.
- **Two zero-comparison parity worlds**, named above, reproducing a known
  instrument defect that is left open.

## What this does not establish

**This is one case on one map.** `red_out_L1_stand` on `mp/ctf_kejim`,
checksum `-1301924053`, one leg (`t1e83`) from one start cell. Rows 1, 4 and 5
of the gate are **not measured here**: row 1 has only its comparison-count
signal reported and its named defect is open, row 4 needs every level-1 case
timed and only the stand case is, and row 5 - the cost row - needs the search
enabled without the parity harness under a declared per-frame CPU budget with
four bots, which no world here ran.

Three worlds per class is **half** the six matched repetitions row 2 asks for.
The times are identical within every cell, which is the quantisation these
stand runs are already known to have and not evidence that three is enough; the
row's own count is what it asks for.

**Row 2's median being met is a median over four numbers.** Two of the four
classes are at or under 16,000 ms and two are not, and the median falls between
them. It is not a claim that a typical start completes in 16,412 ms, and the
declared-weight mean over the same worlds is 17,438 ms.

**Nothing here says the class-100 or class-150 explorer results were wrong.**
Their branch worlds completed in 16,725 and 15,975 ms and those worlds are
retained. What is shown is that the tape extracted from each does not
reproduce that trip from a fresh start, so those times cannot be carried
forward as tape times. Whether a re-extraction, a different tie rule, or an
exit-state term in the decision identity would make them reproduce is not
tested here; the class-75 record already flagged that the landing-cell identity
is coarser than the outcome.

A tape is bound to the map checksum and the physics settings, and a different
server frame or Pmove step invalidates it by construction. Contention is out of
scope.

## Tools and files

No C source changed. `tools/movement_tape_library.py` built the library
unchanged; `tools/movement_bench.py --decision-library` staged it and set the
cvar, naming no tape; `tools/movement_leg_budget.py` produced row 3 unchanged.
The campaign's own scripts (`run_four.py`, `run_four.sh`, `verify_sources.py`,
`analyse.py`) are retained beside its worlds and are not project tools. The
Python suite passes from the repo root.
