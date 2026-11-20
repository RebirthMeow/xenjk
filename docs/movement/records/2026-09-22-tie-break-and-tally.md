# The tie-break and the parity tally: four tapes reproduce, and a world's comparison count is its own

Recorded 2026-09-22. Two repairs and one instrument change, measured together
on a fresh build: the decision tape's tie-break, the parity harness's
end-of-leg tally, and a confirmation world in the decision explorer.

> **All four explorer tapes now reproduce.** With the tie between several
> matching admitted candidates broken by the printed-aim gap before the judge's
> rank, the class-100 tape hits **11 of 11** entries where it hit 4, and the
> class-150 tape **10 of 10** where it hit 2. Their times are the explorer's
> own: **16,725 ms** and **15,975 ms**, against 16,875 and 18,675 on the
> previous build. Classes 75 and 125 are unchanged at 15,950 and 15,925 with
> every entry hit, which is the regression the repair had to pass.
>
> **Row 2 is met on these worlds, on three repetitions of the four declared
> classes rather than the six it asks for.** Equal-weight median **15,962.5 ms**
> against the row's 16,500, tail **16,725 ms** against 18,000, control on the
> same build 18,475 and 21,125. The declared-weight mean is **16,054.6 ms**
> against a control's 17,427.7 and the gate row's last figure of 17,394.
>
> **Every world reports a real parity comparison count.** `Move_ParityTally`
> emits one tally per leg where the tape census is emitted; the 18 completed
> worlds report **667 to 905** comparisons and **zero** mismatches, and no
> world reports zero. Where a world also printed a periodic `horizon` event,
> the two agree to the frame in all 18.
>
> Zero deaths and the movement owner on every frame of every completed world.
> Rows 3, 4 and 5 are not measured here.

## What was asked

Repair the tie-break in `bot2_move.c` so that, when more than one admitted
candidate matches a tape entry, the smallest printed-aim gap is preferred and
the judge's rank decides only a tie on that gap, with the aim staying out of
the match criterion. Implement the safe parity repair the
[tape-library record](2026-09-22-tape-library.md) describes and change
`movement_bench_lib.verdict` and `parity_summary` with it. Add a confirmation
world to the decision explorer. Then build fresh, reproduce both control cells
with the library off, and measure the four-key library against a control per
class.

## Why the tie-break was the defect

The [four-class library](2026-09-22-four-class-library.md) found every library
world selecting on the exact rung and two of four tapes not reproducing, and
named the shape of the problem rather than the cause: the class-100 and
class-150 tapes had never been run on a fresh world, and 7 of 11 entries
typically have several admitted candidates matching by the landing-cell
identity.

The [class-75 campaign](2026-09-22-decision-explorer-c75.md) had already
measured that the identity groups branches the trip does not: at one decision
five candidates it calls one decision complete in five times spanning 1,625 ms.
So which of several matches is taken decides the trip, and it was decided by
`results[i].score` - the judge's own ranking. A score is computed from the live
root, and the explorer's branch world reached that root under a prefix tape
plus a forced index while a fresh tape-only world reaches it under the tape
alone. The class-75 campaign measured that displacement directly: root speed
**442.3 against the parent's 437.5** at its third decision, enough to move the
rolled-out landing cells. A tie broken by a quantity that differs between the
two worlds is a tie broken differently in the two worlds, and that is what the
two tapes hit 4 and 2 entries for.

**The repair.** The tie is broken by the printed-aim gap first - the same
quantity the census already computes and reports, `|Wrap(candidate aim -
entry aim)|`, summed over both hops when the entry names two - and the judge's
rank decides only an exact tie on it (`MOVE_TAPE_AIM_TIE`, 1e-6 degrees). The
aim is a property of the candidate, not of the ranking, so the candidate the
tape's own world took is the one nearest in aim from either world. Nothing else
moved: the match criterion is still kind, hop count, both run-ups and the
landing cells or the graph edges, and the aim is still never a criterion.
Admission, physics, cadence and the ranking weights are untouched, and
`matches=` and the chosen gap are recorded as before. The entry now carries
both hops' printed aims as numbers (`aimValue`, `aimValue2`); hop 0's is still
the gap the census prints.

**What it did.** Per class, on three fresh worlds each, the tape's own entries:

| Class | Entries | Hits before | Hits now | Entries with several matches | Every gap | Time before | Time now | Explorer's own |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 75 | 11 | 11 | **11** | 6 (up to 6 matches) | 0.0000 | 15,950 | **15,950** | 15,950 |
| 100 | 11 | **4** | **11** | 5 (up to 4) | 0.0000 | 16,875 | **16,725** | 16,725 |
| 125 | 11 | 11 | **11** | 6 (up to 3) | 0.0000 | 15,925 | **15,925** | 15,925 |
| 150 | 10 | **2** | **10** | 6 (up to 6) | 0.0000 | 18,675 | **15,975** | 15,975 |

The two tapes that did not reproduce now return their explorer campaigns'
times **to the millisecond**, and the two that did are unchanged. The tie-break
fires at 5 or 6 entries of every tape, so this is not a repair that happens to
leave the passing cells alone by never running: it runs at half the entries of
every leg and picks the same candidate the previous rule picked wherever the
previous rule was right.

## The parity tally

The [tape-library record](2026-09-22-tape-library.md) established that the
zero-comparison worlds were the tally's print condition and not the sampler's
coverage: `Bot2_MoveParityAfterPmove` counts every matched frame in
`parity.compared` and prints a `horizon` event only when the horizon is
exhausted **and** (`parity.rollouts % 10 == 0` or the horizon covered a
committed sequence), and `parity_summary` reported the largest `frames=` it saw
on such an event. Printing on every exhausted horizon is the repair that is not
safe - a per-frame-path print has moved a trip by 2,000 ms once already
([lessons](../LESSONS.md)) - so the repair is the one that record names:

- **`Move_ParityTally(client)`**, called from `Move_Reset` beside
  `Move_DecisionTapeCensus`, so it prints once at the leg boundary the census
  prints at. It emits `event=parity reason=tally` with
  `frames=<parity.compared>` and the same fields the periodic event carries,
  plus `index=`. It prints only when `parity.compared` has risen since the last
  tally, so a leg that compared nothing new prints nothing and a world that
  genuinely compared nothing still reports zero - which is what gate row 1's
  non-zero requirement is for.
- **`movement_bench_lib.parity_summary`** reports `compared` as the tally's
  count when a tally is present and falls back to the largest `horizon` count
  when it is not, so every retained world from before the tally is read exactly
  as it always was. `tallies`, `tallied` and `horizon_frames` are reported
  beside it, and `row1_ok` states gate row 1's parity requirement outright:
  zero mismatches **and** a non-zero comparison count.
- **`movement_bench_lib.composition_verdict`** exempts `tally` as it exempts
  `horizon` and `forecast_refused`. A world is not failed for reporting how
  much it compared; a non-zero real count with zero mismatches passes, and a
  zero real count fails row 1 through `row1_ok` rather than through the
  verdict, which is where the other rows' evidence is judged too.

Seven unit cases on hand-built logs in `tools/test_movement_bench.py`: the
tally being the count rather than the largest horizon event, a world with no
horizon event at all still reporting its count, a pre-tally log read as it
always was, a genuinely zero count failing row 1, a mismatch failing row 1
however much was compared, the tally not being a mismatch and not failing a
world, and the last tally of a world being its count.

### The four-class campaign's two zero-count worlds, from their own logs

`.codex-research/tape-library-four-20260922-a`, `c75-ctl-1` and `c75-ctl-3`,
read-only and not re-run:

- **All three class-75 controls carry the same six armed coverage lines**, with
  `parity_index` 52, 10, 10, 10, 6 and 3 against horizons of 70, 256, 256, 81
  and 256. The three worlds compared the same frames at the same horizons.
- **`c75-ctl-2` printed two `horizon` events and its siblings printed none**:
  `frames=64 rollouts=80 planned=0` and `frames=70 rollouts=110 planned=0`.
  Both printed because `rollouts%10==0`, neither because the horizon covered a
  committed sequence. `c75-ctl-1` and `c75-ctl-3` carry **no parity event of
  any reason at all**, which is why their summaries read zero.
- A tally at the leg end would have printed `parity.compared` in all three. On
  the repaired build the same cell - class 75, tape off, 21,125 ms, 15 straight
  and 7 state commits - reports **905 comparisons at rollouts 1,060**
  (`c75-ctl-1`) and **905 at 1,100** (`ctl-c75-0-a2`). So `c75-ctl-2`'s own
  reported 70 was not that world's count either: it was the count at rollout
  110 of a trip that reaches about 1,100, and the two worlds reported as zero
  compared about the same 900 frames their sibling did.

### On the fresh worlds

All 18 completed worlds emit exactly one tally and report **667 to 905**
comparisons with **zero** mismatches; `row1_ok` is true for all 18. Every one
of them also printed at least one `horizon` event, and **the largest horizon
count equals the tally in all 18**, which is the check that the tally is the
same counter and not a second one. No world of this campaign reported zero, so
the repair's effect on a zero-count world is shown by the reasoning above and
not by a fresh instance of it.

## The explorer's confirmation step

`tools/movement_decision_explore.py`. Before a branch world's time can become
the incumbent, one more world runs **its whole sequence as a tape, with no
prefix and no forced index** - the experiment a tape is asked for later, which
a branch world is not.

- `Campaign.confirm(record)` builds the branch's own leg into a full tape
  through `movement_decision_tape.tape_text` and runs it as `<branch>-cf`
  through the same `world` path as every other world, so it is retained,
  appears once in `progress.jsonl` and resumes without re-running.
- `Campaign.select(grown, width)` confirms in **branch-time order and stops
  when the beam is full**, so the confirmation costs one world per beam slot
  and one per rejection, not one per branch.
- `confirmed(record)` requires the confirmation to have completed and its
  census to show one hit per entry with nothing refused and nothing absent;
  `score_time(record)` returns the confirmation's time for a branch and the
  world's own time for the control, which needs no confirmation because it ran
  under no tape. The beam, the incumbent, the patience history and the reported
  best all read `score_time`.
- A branch whose confirmation does not hit every entry is **recorded and
  excluded**: it appears under `unconfirmed` in the campaign summary with its
  branch time, its confirmation's time and that confirmation's census totals.
- A confirmation world is never itself a node, so a rejected sequence cannot
  re-enter the beam under another name.

Nine unit cases on hand-built worlds in
`tools/test_movement_decision_explore.py`, and the existing 35 updated for the
extra world: the confirmation running the whole sequence with no force and no
prefix, the confirmed time being what the beam and the report carry, a
confirmation that misses an entry excluding and recording its branch, a
confirmation world not being a node, the control confirming nothing, a branch
with no confirmation not being confirmed, a refused entry not confirming, only
the beam's own width being confirmed, and a resumed campaign not re-running a
confirmation.

Had this existed, the class-100 and class-150 campaigns would have run one
confirmation world at each depth's beam and their stopping rules would have
read 16,875 and 18,675 ms rather than 16,725 and 15,975. With the tie-break
repaired the two now agree, which is the outcome this step exists to establish
rather than assume.

## Binding

Build `.codex-research/tie-break-build-20260922-a`, manifest
`20260922T103836816430Z.sources.json`, `openjkded.x86.exe` sha256
`83d7a60d700560c06fa6bb39334a802c20d31fb58066fb910b41cf528c201157`,
`jampgamex86.dll` sha256
`2783d9baed803ae83d10f4bd7ce638fce4e2096ee6c43dcd648da30f36aca371`. **All
1,117 source hashes matched the working tree before the first world and again
after the last** (`verify_sources.py`, `manifest-before.json`,
`manifest-after.json`), so no source moved while worlds ran.

The library is the four-class campaign's own, copied beside this campaign and
bound by hash (`manifest-library.json`): all six files identical to
`.codex-research/tape-library-four-20260922-a/library/mp_ctf_kejim_-1301924053/`,
`index.txt` sha256
`64b6e5ef112f22b97c6a76d4e577e38151beab1fbeab6376a02c7fa9b1a593cc`, the same
four keys under leg `t1e83`, cell 0, phases 75, 100, 125 and 150. **No tape was
re-extracted**: the tapes are byte-for-byte the ones that hit 4 and 2 entries
on the previous build, and only the engine's tie rule changed.

Worlds under `.codex-research/tie-break-tally-20260922-a/`, every one retained,
one bench call each, sequential, one detached campaign (`run.py`, `run.sh`,
`progress.jsonl`, `campaign.out.txt`, `campaign.done`). Every world runs the
decision-tape record's own
[settings block](2026-09-22-decision-tape.md#the-settings-every-world-runs),
case `red_out_L1_stand`, the 2026-09-16 seeded graph, `--edges --parity 256`,
`--momentum-trace`, the class pinned with `--corner-class` in the 3,000-5,500
ms window. The bench names no tape in any library world: it stages the
directory and sets `bot_moveTapeLibrary=1`. Control worlds are staged with no
library at all.

## The control cells

The first two worlds, library off and no tape named, before any library world
ran:

| World | Class | Completed | Commits / state | Required |
| --- | ---: | ---: | ---: | --- |
| `ctl-c125-0` | 125 | **15,925 ms** | **13 / 5** | 15,925 / 13 / 5 |
| `ctl-c75-0-a2` | 75 | **21,125 ms** | **15 / 7** | 21,125 |

Both reproduce. `ctl-c75-0` is retained and did not: four fresh worlds returned
`runner_error` with no corner level at all - the fixture never released, which
is not the class ladder missing a phase - and it was replaced under `-a2`.

## The worlds

| World | Class | Arm | Completed | Commits / state | Rung | Census | Parity compared / mismatches |
| --- | ---: | --- | ---: | ---: | --- | --- | ---: |
| `ctl-c125-0` | 125 | control | 15,925 ms | 13 / 5 | - | - | 667 / 0 |
| `ctl-c75-0` | 75 | control | pin miss | - | - | - | - |
| `ctl-c75-0-a2` | 75 | control | 21,125 ms | 15 / 7 | - | - | 905 / 0 |
| `c75-lib-1` | 75 | library | **15,950 ms** | 14 / 2 | **exact** | **11 of 11 hit**, 0 refused, 0 absent, deferred 1 | 718 / 0 |
| `c75-ctl-1` | 75 | control | 21,125 ms | 15 / 7 | - | - | 905 / 0 |
| `c75-lib-2` | 75 | library | **15,950 ms** | 14 / 2 | **exact** | **11 of 11**, 0, 0, deferred 1 | 718 / 0 |
| `c75-lib-3` | 75 | library | **15,950 ms** | 14 / 2 | **exact** | **11 of 11**, 0, 0, deferred 1 | 698 / 0 |
| `c100-lib-1` | 100 | library | **16,725 ms** | 14 / 4 | **exact** | **11 of 11**, 0, 0, deferred 5 | 739 / 0 |
| `c100-ctl-1` | 100 | control | 19,375 ms | 13 / 7 | - | - | 825 / 0 |
| `c100-lib-2` | 100 | library | **16,725 ms** | 14 / 4 | **exact** | **11 of 11**, 0, 0, deferred 5 | 759 / 0 |
| `c100-lib-3` | 100 | library | **16,725 ms** | 14 / 4 | **exact** | **11 of 11**, 0, 0, deferred 5 | 719 / 0 |
| `c125-lib-1` | 125 | library | **15,925 ms** | 13 / 5 | **exact** | **11 of 11**, 0, 0, deferred 2 | 687 / 0 |
| `c125-ctl-1` | 125 | control | 15,925 ms | 14 / 5 | - | - | 687 / 0 |
| `c125-lib-2` | 125 | library | **15,925 ms** | 14 / 5 | **exact** | **11 of 11**, 0, 0, deferred 2 | 667 / 0 |
| `c125-lib-3` | 125 | library | **15,925 ms** | 13 / 5 | **exact** | **11 of 11**, 0, 0, deferred 2 | 687 / 0 |
| `c150-lib-1` | 150 | library | **15,975 ms** | 14 / 1 | **exact** | **10 of 10**, 0, 0, deferred 1 | 699 / 0 |
| `c150-ctl-1` | 150 | control | 17,575 ms | 16 / 8 | - | - | 743 / 0 |
| `c150-lib-2` | 150 | library | **15,975 ms** | 14 / 1 | **exact** | **10 of 10**, 0, 0, deferred 1 | 679 / 0 |
| `c150-lib-3` | 150 | library | **15,975 ms** | 14 / 1 | **exact** | **10 of 10**, 0, 0, deferred 1 | 699 / 0 |

Every library world selected on the **exact** rung and named the tape its phase
indexes, as on the previous build; selection was never the defect and is not
now. **Every entry of every tape is a hit with an aim gap of 0.0000** in all
twelve library worlds, and the three worlds of each class agree to the
millisecond and decision for decision. Zero deaths and the movement owner on
every frame of all 18 completed worlds.

The class-125 commit counts differ between worlds of the same cell (13/5 and
14/5) while the completed time does not; that is the return past the objective,
which the leg's 11 decisions do not cover, and the same split appears in the
control.

## Row 2, as the gate states it

| Class | Library (3 worlds) | Control (1 world) | Change |
| ---: | ---: | ---: | ---: |
| 75 | **15,950** | 21,125 | **-5,175** |
| 100 | **16,725** | 19,375 | **-2,650** |
| 125 | **15,925** | 15,925 | 0 |
| 150 | **15,975** | 17,575 | **-1,600** |

| Row 2 | Library | Control | Gate |
| --- | ---: | ---: | ---: |
| Equal-weight median | **15,962.5 ms** | 18,475 ms | 16,500 - **met** |
| Tail (worst completed world) | **16,725 ms** | 21,125 ms | 18,000 - **met** |

**Both of row 2's numbers are met on these worlds**, and the row is **not
reported as passed**: it asks for at least six matched repetitions per class
and this is three, with one control per class rather than three. The times are
identical within every cell, which is the quantisation these stand runs are
already known to have and not evidence that three is enough. Rows 3, 4 and 5
are not measured here, and the gate is reported only when every row passes on
one build.

**The declared-weight mean, for continuity with earlier records** (weights 4,
12, 30, 47 for classes 75, 100, 125, 150; not the gate's definition):
**16,054.6 ms library against 17,427.7 ms control**, against the gate row's
last figure of 17,394 ms. The four-class campaign's library arm was 17,438 ms
on the same weighting, so the repair is worth **1,383 ms** on the weighting
that class 150 dominates - which is the class the previous build's library made
1,100 ms slower and this one makes 1,600 ms faster.

## What failed

- **One fixture miss.** `ctl-c75-0` exhausted four worlds with no corner level
  at all (`runner_error`, `corner_level None`), so no class was landed and the
  trip never started. Retained; replaced under `-a2`, which returned the
  control cell exactly. This is a different failure from the class ladder's pin
  miss, which lands a world and reads the wrong phase.
- **No fresh zero-comparison world.** Every world of this campaign printed at
  least one `horizon` event as well as its tally, so the repair's effect on the
  world that reported zero is established from the four-class logs and the
  agreement of the two counters on 18 worlds, not from a fresh instance.
- **The confirmation step is unexercised natively.** It is unit-tested and no
  explorer campaign has run under it, because the four tapes it exists to
  protect now reproduce without a re-run.

## What this does not establish

**This is one case on one map**: `red_out_L1_stand` on `mp/ctf_kejim`,
checksum `-1301924053`, one leg (`t1e83`) from one start cell, three
repetitions per class against one control per class.

**Row 3 is not measured on these worlds** and the plan's own rule is that a
row-2 gain with row 3 unchanged is a selection artifact. The previous build's
library arm waited 30.4, 58.3, 37.9 and 62.5 percent at the four classes, with
an `uncommitted_grounded` run at 100 and 150; whether the two repaired classes'
faster trips also wait less is the measurement this record does not carry.
Rows 1, 4 and 5 are likewise not reported: row 1 has its parity signal on 18
worlds and nothing else, row 4 needs every level-1 case timed, and row 5 needs
the search without the parity harness under a declared CPU budget with four
bots.

**The tie-break is a repair to a diagnostic's matching rule, not to the
pipeline.** It changes which of several candidates the identity rule calls one
decision is taken when a tape names that decision; it changes nothing about
what the search proposes, what the judge admits, or how the ranking orders
anything when no tape is loaded. With both cvars unset this build returns the
two control cells unchanged, which is the evidence that it is inert when off.

A tape is bound to the map checksum and the physics settings, and a different
server frame or Pmove step invalidates it by construction. Contention is out of
scope.

## Tools and files

`codemp/game/bot2_move.c` (the tie-break, `aimValue2`, `MOVE_TAPE_AIM_TIE`,
`Move_ParityTally` and its call from `Move_Reset`), `codemp/game/bot2_move.h`
(the tally's declaration), `tools/movement_bench_lib.py` (`parity_summary`,
`composition_verdict`), `tools/movement_decision_explore.py` (the confirmation
step), and the two test files. No tape, library or retained world was
re-written. The campaign's own scripts (`run.py`, `run.sh`, `verify_sources.py`,
`hash_library.py`, `analyse.py`) are retained beside its worlds and are not
project tools. The Python suite passes from the repo root: **816 tests**.
