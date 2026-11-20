# Decision explorer, stage 1: forcing, the clock, the eight classes, the build script

Recorded 2026-09-22. The four checks the
[work order](../../archive/movement-2026-10-05/PLAN.md#next-work-order-2026-09-22-the-decision-explorer)
requires before the search runs, and the explorer tool itself with a smoke run.
No search campaign: stage 2 is not this.

> **Result.** Index forcing is **reproducible to the frame within a class**:
> two forced candidates, two worlds each, and each pair agrees on the completed
> time, on every commit of the leg and on the tape census - one pair from two
> different release levels of class 75. A world costs **16.9 s median** (9.5 to
> 35.3 s, 27 completed worlds) at the bench's default timescale, and a bench
> call that never meets its class costs about 26 s. **All eight start classes
> are reachable**, and the four the harness had never landed - 0, 25, 50 and
> 175 - were landed here; a world's play ladder offers exactly four of the
> eight, so the other four need another world. `strafe_fixture_build.py` now
> exits non-zero on a failed build, with six regressions that fail before the
> repair.
>
> Two things the checks found that the explorer had to be built around: the
> forced-candidate override is offered on the bot's **pre-release** commits
> too, because the commit counter restarts at the corner release, and a leg
> that ends on an **arriving** hop has a last decision no tape can name.

## Binding

Build `.codex-research/decision-tape-build-20260922-f`, manifest
`20260922T061108642831Z.sources.json`: all **1,117 source hashes matched the
working tree** before and after collection, so no rebuild was needed and the
control cell did not have to be re-established.
`openjkded.x86.exe` sha256 `ec5fe3f0f16751cc7546843334c7b3e5a757027c23f2f9df3d010685c627861c`,
`jampgamex86.dll` sha256 `52238e80ac58c09508acbe754562e2a971d6d1d191a03a9aa458a669be6b0e2c`.
No C source was touched by this work at all.

Worlds are retained under `.codex-research/decision-explorer-stage1-20260922-a/`
(`run_stage1.py`, `run_stage1b.py`, `summarize_stage1.py`, `progress.jsonl`,
`summary.json`) and the explorer's smoke runs under
`.codex-research/decision-explorer-smoke-20260922-{a,b}/`. Every world runs the
decision-tape record's own
[settings block](2026-09-22-decision-tape.md#the-settings-every-world-runs),
case `red_out_L1_stand`, the 2026-09-16 seeded graph, `--edges --parity 256`,
one world per bench call, sequential, never in parallel batches, and each
campaign was launched detached with its progress file.

## 1. Index forcing is reproducible within a class

**Where the branch is.** In every retained class-75 transfer world
(`.codex-research/decision-tape-transfer-20260922-b/tape-c75-*`) the tape's
entry 0 is hit at level 4325, which the commit line numbers `commit_nth=1`. The
commit that follows it is the leg's second, `commit_nth=2` at level 6850: 24
candidates, 12 of them admitted, and the tape's entry 1 among the refused. So
the branch ordinal is **2**, and because the prefix's next entry is refused
there the prefix cannot take the frame away from the forced index - the tape's
preference runs after the override in the same block and selects only what the
judge admitted.

**What was forced.** Two admitted candidates of that search, read from the
retained world's own `[SPIKE]` lines, chosen as far apart as the set allows:

| `bot_moveSelectCand` | kind | hops | run-ups | landing cells |
| ---: | --- | ---: | --- | --- |
| 3 | straight | 2 | 24, 0 | 9197, 5650 |
| 15 | state | 1 | 24 | 9197 |

Two worlds each, under the 11-entry class-125 leg tape from
`decision-tape-verify-20260922-g` as the prefix, with `bot_moveSelectNth=2`.

| Pair | Release levels | Completed | Straight / state commits | Leg decisions | Census |
| --- | --- | ---: | ---: | ---: | --- |
| cand 3 | 4075 and **3675** | 20,275 ms both | 16 / 8 both | 12 both | `hits=1 refused=1 absent=3 deferred=3` both |
| cand 15 | 3075 and 3075 | 19,025 ms both | 12 / 5 both | 11 both | `hits=1 refused=1 absent=0 deferred=1` both |

**Each pair is identical to the frame.** Beyond the table, the two worlds of a
pair agree **decision for decision** on everything the commit line prints -
kind, hop count, both aims as printed, both run-ups, both landing cells, both
graph edges, the commit ordinal - with each decision's level taken as an offset
from that world's own release. The cand-3 pair did this from **two different
release levels of the same class**, 400 ms apart, which is the statement the
explorer needs: the class is the state, not the absolute level.

The reference world in the same directory, same tape, force off, completes in
**21,125 ms** - the transfer collection's class-75 cell, reproduced on a third
campaign.

**A by-product, not a gate row.** Forcing the reachable prefix's next decision
is faster than the ranking's own choice at class 75: 21,125 ms unforced,
**20,275 ms** forcing candidate 3 and **19,025 ms** forcing candidate 15, each
twice. Two of twelve admitted candidates, so the beam has something to find.
This is a diagnostic under forced selection and moves nothing.

**What the check found.** The override is offered wherever `m->commits+1`
matches, and the commit counter **restarts at the corner release**, so the
bot's own pre-release walk carries the same ordinals: every forced world prints
`select_forced=-1` on a pre-release commit and `select_forced=1` on the leg's.
The release teleports the runner, so nothing about the trip depends on what the
override did before it - but a reader that does not filter by the release
reports every branch as a branch that did not happen, which is exactly what the
explorer's first smoke run did
(`.codex-research/decision-explorer-smoke-20260922-a/`, retained). The reader
now takes the leg's commit and has a regression.

## 2. The wall clock of a world

From the 27 completed worlds of this directory and the transfer collection, at
the bench's default `--timescale 5`, including the pinned-class wait and the
server's start-up and shutdown:

| | seconds |
| --- | ---: |
| median | **16.9** |
| mean | 18.0 |
| fastest | 9.5 |
| slowest | 35.3 |
| a bench call whose four worlds never met the class | ~26 (three observed: 25.7, 26.2, 26.6) |

About one call in eight misses its class outright, so a branch costs about
**20 s expected**. At class 75 the first decision offers 21 admitted candidates
and the second 12, so a beam of 3 is roughly **12 to 21 minutes per depth** and
an 11-decision leg is a few hours: a detached campaign, as the order says, not
a foreground one.

## 3. Which of the eight classes the harness can land

**The mechanism.** The class is `level.time` modulo 200, and the only clock the
runner can read is the play log, which `Bot2_PlayLogSample` writes at 4 Hz:
the first row on the runner's first think at some level `L0`, then `next =
level.time + 250`, so the ladder is `L0 + 250k` (transition rows - death,
possession, capture, objective - re-anchor it). `gcd(250, 200) = 50`, so a
world can only ever be started on the **four classes congruent to `L0` modulo
50**, and `L0` is a multiple of the 25 ms frame. There are therefore two
families, `{0, 50, 100, 150}` and `{25, 75, 125, 175}`, and a world offers one
of them.

**The evidence.** The 40 retained play logs of the two decision-tape campaigns
start at 2350 (24 worlds), 2375 (15) and 1900 (1): families 0 and 25, about
three worlds in five on the even family. That is why the harness had landed
75, 100, 125 and 150 - two from each family, and nobody had asked for the rest.

**The probe.** One bench call per class for the four never landed, same
settings, same window `[3000, 5500]`:

| Class | Landed | Release level | Completed | Worlds used of 4 |
| ---: | --- | ---: | ---: | ---: |
| 0 | yes | 3600 | 18,525 ms | 1 |
| 25 | yes | 3625 | 18,025 ms | 3 |
| 50 | yes | 3850 | 18,600 ms | 1 |
| 175 | yes | 3575 | 19,675 ms | 1 |

**All eight classes are reachable**, and none of the four is a special case.
What the window does is bound the wait, not the class: every class has members
in `[3000, 5500]` on either ladder. The cost is that a class of the family a
world did not start on needs **another world**, and the bench gives a pinned
class four; that is a ~13 percent chance of a call missing on the odd family
and ~3 percent on the even one, which is not a guess - three of the four
forcing worlds of the first stage-1 run missed class 75 that way, at 26 s each,
and landed on a second call under a fresh name
(`run_stage1b.py`, worlds `*-b1`).

The explorer therefore covers all eight classes, and retries a world that never
landed under a new name rather than in place.

## 4. `strafe_fixture_build.py` exits 0 on a failed build

The trap the decision-tape record left open: one rebuild compiled the dedicated
engine, failed the game DLL and the script reported success; only the absent
hash manifest kept the broken fixture out of a campaign, and
`movement_bench.stage_build` refusing a build with no manifest is a guard that
held by accident.

The repair does not change what is built. After the builder returns, and before
the manifest is written, two things are checked directly: **MSBuild's own
verdict line** in the build log (`Build FAILED.`) whatever the process exit code
was, and the **two binaries every world stages** (`openjkded.x86.exe`,
`jampgamex86.dll`). Either failing prints the log tail and returns **2**,
leaving no manifest.

`tools/test_strafe_fixture_build.py`, 8 cases on a stubbed builder - no
compiler runs. Against the pre-repair script **6 of the 8 fail**: the
zero-exit-with-`Build FAILED.` case returned 0, and the missing-binary cases
died in the manifest's own hashing with `FileNotFoundError` rather than a
reported failure. All 8 pass after.

## The explorer

`tools/movement_decision_explore.py`, with
`tools/test_movement_decision_explore.py` (34 cases on hand-built logs).

Depth 0 is the class's control world. To branch at decision *k* of a node, the
node's own first *k* decisions are written as a decision tape through
`movement_decision_tape.tape_text` - one parser for what a decision is - and
one world runs with that tape, `bot_moveSelectNth = k+1` and
`bot_moveSelectCand` naming one admitted candidate of that search, read from
the node's `[SPIKE]` lines at that frame; *k* of 0 stages no tape at all. Every
world runs to the objective, so every branch yields a completed time and a
whole decision sequence, and the best B by completed time carry to the next
depth. A world is kept out of the beam when it did not complete, when the
engine's census says its prefix stopped being taken, or when the commit line
says the named index was not admitted. The search stops when every surviving
prefix covers its leg, or when no branch has improved the incumbent for
`--patience` depths. Worlds run one at a time; every world is retained; the
progress JSONL is the resume, and a world already in it, or already retained
with a `result.json`, is never run again. The per-class result JSON names the
best sequence and writes it as a tape the bench can load with `--decision-tape`.

**A leg's last decision may not be nameable.** A straight plan is named by the
cell each hop lands in, and an arriving hop never lands: `arrives=1` prints
`c0=-1`, and `movement_decision_tape` refuses the whole world rather than name
it - correctly, for a tape. At class 75 most legs end that way (three of the
nine worlds here). The explorer reads the leg up to that commit, records its
level, and branches no further; the decisions before it are exactly the ones a
prefix can carry. Nothing about the tape format or the identity rule changed.

**The smoke run**, `.codex-research/decision-explorer-smoke-20260922-b/`, class
75, depth 1, three branches of the first decision:

| World | Branch | Forced | Completed |
| --- | --- | ---: | ---: |
| `control` | - | - | 21,125 ms |
| `control-d1c0` | `nth=1 cand=0` | 1 | **19,725 ms** |
| `control-d1c1` | `nth=1 cand=1` | 1 | **18,950 ms** |
| `control-d1c2` | `nth=1 cand=2` | 1 | 21,000 ms |

Four worlds, all complete, one depth, stopped on `--max-depth 1`; the best
sequence was written as an 11-entry tape,
`tapes/c75-best.tape`, from the 18,950 ms world, with the arrival at level
22000 recorded as the commit no tape can name. The tool drove the bench
correctly: it read the control's first decision and its candidate set, staged
no tape at depth 0, set the override, and the engine confirmed the forced index
fired on the leg's own commit in each branch. Two of the three branch times
reproduced the first smoke run's to the millisecond, which is a third pair of
worlds agreeing within a class. The first smoke run at
`decision-explorer-smoke-20260922-a/` is retained as it ran, with its
misreported `forced=-1`.

## What this does not establish

No search was run: this is stage 1 and the beam has taken one depth in a smoke
run with three of 21 branches. The forcing pairs are two candidates at one
decision of one class of one case on one map, and reproducibility within a
class is not reproducibility of anything else. The faster forced times are a
diagnostic under forced selection and move no gate row, as stage 3 of the order
says: no explorer time counts until the bot selects a tape itself from the map,
leg, start cell and its own phase, and the fallback is measured. The class
probe says each of the four classes can be landed, not that each is as cheap:
one bench call each is not a rate. The build-script repair is exercised against
a stub, not against a real failed compile.
