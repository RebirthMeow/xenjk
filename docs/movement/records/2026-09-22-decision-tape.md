# The decision tape: the instrument, and the six defects the self-control found

Recorded 2026-09-22. The instrument the
[fifth revision](../../archive/movement-2026-10-05/PLAN.md#steps-2-and-3-revised-2026-09-21-the-decision-tape)
ordered after the open-loop replay was withdrawn, and its closed-loop
self-control.

> **Result: the self-control passes.** On the final build, one class-125 world
> with the tape unset completes `red_out_L1_stand` in **15,925 ms with 13
> commits, 5 of them state**, and one world executing the tape built from that
> world's own telemetry completes in **15,925 ms with the same 13 and 5, every
> one of the leg's 11 entries a hit, no miss, and every aim gap 0.0000**.
>
> **Collection then answered the transfer question, and not with one row of the
> table.** Entry 0 transfers from every start; the sequence then stops at entry
> 1 or 2 for a class-specific reason - refused at class 75, absent at 100 and
> 150 - and no class reaches 15,925 ms or hits more than 2 of 11 entries. The
> class-125 sequence is not available whole from any other start. Forcing the
> prefix that is available is 450 ms faster at class 100 and 550 at class 150.

It took six runs to get there. Each failed differently and each failure was a
real defect in the instrument, named by the telemetry rather than guessed at.
They are recorded because the last row of the order's reading table exists for
exactly this, and because most of them are mistakes an instrument like this
invites. The sixth was found by the census itself on its first working run.

Builds `.codex-research/decision-tape-build-20260922-{a,b,c,d,e,f}`; worlds
under `.codex-research/decision-tape-verify-20260922-{a,b,c,d,e,f,g}/` and the
first collection under `.codex-research/decision-tape-transfer-20260922-a/`,
all retained.

## What a decision tape is

Not a command tape. The
[open-loop replay](2026-09-21-bot-route-recorder.md) reproduced the recorded
commands and lost one landing to 0.28 units; a different start class perturbs
the state by construction, so that instrument could not have separated the two
readings even repaired. A decision tape names *what was chosen*, and the world
executing it still searches, rolls out and judges from its own live state, and
still refuses what is not admissible there. A refusal is then the measurement
rather than a broken run.

**`tools/movement_decision_tape.py`** builds one, read-only, from a retained
world's `[MOVE] ... event=plan edge=hop0 reason=sequence` lines - the commit
line, one per committed sequence. Each entry carries the root the decision was
taken from, the kind, the hop count, both hops' aim and run-up as the engine
printed them, and the plan's graph edges. Aims are carried as printed text, not
as numbers: the match compares what two worlds print rather than two floats
agreeing to a tolerance somebody chose.

**`bot2_move.c`** loads it from `bot_moveDecisionTape`, a default-off cvar
naming a file under `bot2-decisions/`, bound to the live map and checksum the
way the corridor is and refused with a reason rather than repaired. The
preference sits exactly where the forced-candidate override sits - after
`StatePreferred`, the last thing that can change the winner, and before the
route is installed - and keeps that hook's rule: **it can only select a
candidate the judge admitted.** Admission, physics, cadence and the ranking
weights are untouched.

**`movement_bench.py`** stages the file and sets the cvar through
`--decision-tape`, the same shape as `--corridor`.

## The six defects

### 1. A commit on the release frame belongs to the old trip

`bot_corner_start` is issued over rcon and executed in a server frame the game
has already stepped, so a world released at level L can carry one commit at
exactly L taken from wherever the bot's own AI had walked to. Observed: a
commit at level 3525 from (186.4, -496.6) in a world released at 3525 at the
stand. The extractor filtered `>= release`;
`movement_bench_lib.runner_rows` has always been strict for the same reason.
Fixed to `> release`, with two regressions shown failing first.

### 2. A planned edge does not identify a state decision

The state search offers the same planned edge pair twice, tapping at once or
after the short run-up. Matching on edges alone - which is the order's wording
read literally - selected the run-up-0 variant where the control had committed
the run-up-24 one, **reported it a hit**, and diverged the trip from its first
entry: 19,250 ms against the control's 15,925, one hit and five absent misses.

### 3. The cursor cannot ride on `Move_Reset`

An objective change calls `Move_Reset` mid-trip - `reason=objective` at level
22775 in that same world - and the cursor restarted with the flag already in
reach, visible as the tallies going from `hits=1 absent=3` back to `hits=0
absent=1`. The cursor and tallies now start at the corner release and nowhere
else, through `Move_DecisionTapeReset` called beside the recorder's own arming.
That also keeps the bot's pre-release wandering from consuming entries.

### 4. A decision is both hops, and the commit line printed one

With 2 and 3 repaired, both worlds ran at corner level 3125 and the trip still
diverged. The two entry-0 commits were the evidence:

| | control | tape |
| --- | ---: | ---: |
| kind / hops / d0 / r0 / index / index2 | state 2 -0.105301 24 50732 26663 | identical |
| launch_speed / land_speed (hop 0) | 250.1 / 364.7 | identical |
| `land1_speed` | 514.7 | **506.8** |
| score / progress | 515.7 / 853.7 | 510.3 / 852.3 |

Same world, same level, same root, same first hop, different second hop. Two
candidates for one planned edge pair agree on everything the commit line
printed and fly different chains. The straight search's commit line was the
only one printing just `d0 r0`; the spike commit detail and the per-candidate
`[SPIKE]` line have carried `d0 r0 d1 r1` all along, which is why the order
named all four fields - the telemetry had not caught up. `d1` and `r1` joined
that line, the tape carries both hops, the match compares every hop the plan
commits, and the tape format went to 2 so a version-1 tape is refused rather
than matched on half a decision.

### 5. An entry is answered by a decision, not by passing near its root

The trip was then exactly right - 15,925 ms, 13 and 5, every consulted root on
the tape's own root - and the bookkeeping was still wrong: 11 hits and 2 absent
misses.

| Entry | Consulted at | Search outcome | Committed |
| --- | --- | --- | --- |
| 1, state, edges 36420/38065 | level 5950, root 283.0,-231.5 | `reason=none`, nothing admitted | level **6075** at 295.1,-201.8, those edges |
| 6, state, edge 32610 | level 13500, root 2173.4,3069.9 | `reason=none`, nothing admitted | level **13600** at 2197.3,3099.8, that edge |

A search that admits nothing has not made a decision; the bot is walking. But
the entry's root was inside the 64-unit window, so the diagnostic consumed it,
and the bot committed that very decision 125 and 100 ms later with the cursor
already past it. Two misses were reported against a run that made all thirteen
decisions correctly - the failure mode a self-control exists to catch. Now the
cursor holds when the search commits nothing and the frame is counted as
`deferred` rather than recorded.

### 6. A tape is one leg, and this one carried two decisions from the next

Found by the census on its first working run, which reported `hits=11
... deferred=2` with two entries never seen. The class-125 world is released at
3125 and takes the flag at 19050 - and commits again at 19100 and 20375, on the
way back. `red_out_L1_stand` ends at the pickup, so those two are another
leg's decisions. The extractor took every commit after the release, so the tape
carried 13 entries where the leg has 11, and every class's hit count in the
collection would have mixed two legs. A tape is now trimmed to the leg: after
the release, no later than the objective, taken from the runner's recorded
pickup, then from the first objective change in the log, and a world with
neither is carried whole with the sidecar saying so.

## The settings every world runs

Case `red_out_L1_stand`, `--corner-class 125`, one world per arm, sequential,
settings as the rescue screen: `bot_moveSuccessorAim=1 bot_moveReroute=1
bot_moveExitSpeed=500 bot_moveStatePrefer=1 bot_moveSuccessorFan=0
bot_forecastItems=0 bot_forecastTriggerShadow=0`, playback and heading sweep
off, the 2026-09-16 seeded graph, `--edges --parity 256`.

## What identifies a decision, declared before collection

The first collection, run under an earlier rule, returned zero hits at every
class and is retained at
`.codex-research/decision-tape-transfer-20260922-a/`. It is **not** read as a
result: the rule compared the printed aim for equality, and an aim is derived
from the live root and path, so a different start class perturbs it by
construction. Entry 0 at classes 75 and 100 was the same decision as the
tape's - same kind, hop count, planned edges 50732/26663, run-up 24 - and
missed on the aim's fourth decimal, with the engine's own line reading
`near=state gap=0.000`.

The owner then asked for the tolerance to be measured rather than picked. In
the class-125 world's own judged sets after the release, among admitted
straight candidates agreeing on hop count and both run-ups:

| | |
| --- | ---: |
| such pairs | 166 |
| pairs identical in both aims as well | **7** |
| smallest nonzero aim separation | **0.030304 deg** |
| pairs separated by under 1 deg | 86 of 166 |
| aim perturbation at entry 0 across classes | up to **0.066 deg** |

The measurement refutes the premise. Seven pairs are indistinguishable at any
tolerance, and the cross-class perturbation is larger than the in-set spacing,
so no tolerance both admits the same decision from a different start and keeps
a match unambiguous. The aim was never the discriminator inside a set.

**The rule, by owner decision 2026-09-22.** A decision is named by where it
goes, and everything compared is discrete and exact:

- a **state** plan: kind, hop count, both run-ups, `edge` and `edge2`;
- a **straight** plan, which has no edges: kind, hop count, both run-ups, and
  the graph cell each hop lands in.

Aims are carried and reported - on every hit, and on an absent miss as the
nearest candidate's gap - as information and never as a criterion.

**The tie rule.** Two admitted hops of the same shape landing in the same cells
are the same decision for this question. When more than one admitted candidate
matches, the judge's own ranking chooses among them and the count is recorded
as `matches=`. This is not hypothetical: in the self-control, 7 of 11 entries
had two or three matching admitted candidates, including a state entry, and the
tie-break reproduced the control exactly.

**The caveat.** A landing cell is a rollout result, not a plan field, so this
identity exists only for candidates that were rolled out. That covers every
candidate the hook can select, since it selects only admitted ones, and every
entry on a tape, since each was a committed rollout. **It would not serve as a
plan identity for the explorer**, which must name a hop before rolling it out;
it is the right identity for a diagnostic asking whether the same hop is
admissible from a different start.

**A tape is one leg.** Commits strictly after the release and no later than the
objective. The world keeps playing after the flag: the class-125 world is
released at 3125, takes the flag at 19050 and commits again at 19100 and 20375
on the way back. The leg has **11 decisions**; the world makes 13 commits.

## The cursor

Only a hit establishes position on the tape, so only a hit advances the cursor,
and the lookup scans forward from it for the first entry whose root is within
64 units of the live root. Entries a hit passed over are answered at that
moment, once each, as absent misses marked `jumped`. A miss of either kind
leaves the cursor: refused on one frame is not proof the bot has passed the
site, and two frames on the same candidate may be admitted from a slightly
different state, which is the closed-loop behaviour the diagnostic exists to
observe. A search that admits nothing has made no decision and touches nothing.

Each entry is reported once with its final status, attempts, match count and
aim gap, in a `[TAPE] census` line at the end of the leg. The census prints at
the objective change, not at `G_ShutdownGame`: this harness's world log ends at
the rcon quit line and nothing the DLL prints afterwards reaches it.

## The self-control that passes

| World | Tape | Result | Corner level | Commits | State | Entries |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `control-r1` | unset | pass **15,925 ms** | 3125 | 13 | 5 | - |
| `tape-r1` | its own, 11 entries | pass **15,925 ms** | 3125 | 13 | 5 | **11 hits, 0 refused, 0 absent, every aim gap 0.0000** |

Zero deaths, movement owner throughout, zero refusals in both. The tape-unset
world reproducing the cell is what says the tape's code is inert when nothing
is loaded; the tape world reproducing it with every entry a hit is the
closed-loop self-control the order required. The world makes 13 commits and the
leg has 11 decisions: the other two are the return.

Build `.codex-research/decision-tape-build-20260922-f`, worlds under
`.codex-research/decision-tape-verify-20260922-g/`.

## The transfer collection

`.codex-research/decision-tape-transfer-20260922-b/`, build
`decision-tape-build-20260922-f`, the 11-entry class-125 leg tape from
`decision-tape-verify-20260922-g`. Three worlds per class with the tape on,
alternated in sequence with a tape-off control per class, never in parallel
batches. **18 worlds, all complete, no pin misses, zero deaths, movement owner
throughout.** Every cell returned one distinct value and every triple was
identical, so the three repetitions measure reproducibility and not spread.

| Start class | Control | Tape | Change | Hits | First miss |
| ---: | ---: | ---: | ---: | ---: | --- |
| 75 | 21,125 ms | 21,125 ms | 0 | 1 of 11 | entry 1, state, **refused**, level 6850 |
| 100 | 19,375 ms | **18,925 ms** | **-450** | 2 of 11 | entry 2, straight, **absent**, level 7700 |
| 150 | 17,575 ms | **17,025 ms** | **-550** | 1 of 11 | entry 1, state, **absent**, level 6275 |

Per-entry status, identical across each class's three worlds
(h hit, r refused, a absent, u never reached):

| Class | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
| ---: | - | - | - | - | - | - | - | - | - | - | - |
| 75 | h | r | a | u | u | u | u | u | u | u | u |
| 100 | h | h | a | u | u | u | u | u | u | a | u |
| 150 | h | a | a | a | a | u | u | u | a | a | u |

### What it says

**The first decision transfers from every start.** Entry 0 - the state chain on
edges 50732/26663 taken at the stand - is admitted and committed at all three
classes, with aim gaps of 0.0004, 0.0663 and 0.0072 degrees. Those gaps are
also the vindication of the landing-cell identity: exact-aim matching would
have refused class 100's entry 0 outright, which is what the first collection
did.

**The sequence stops at the second or third decision, and the reason is
class-specific.** This is the result that does not fit one row of the table:

- **Class 75 lands on row 2.** Entry 1's candidate is offered and the judge
  **refuses** it, twice, at the same level in all three worlds. The chain is
  reachable that far and that hop is not admissible from the state class 75
  arrives with.
- **Class 100 lands on row 3**, one step later: entries 0 and 1 both hit, and
  entry 2 is absent with the nearest candidate 5.06 degrees away.
- **Class 150 lands on row 3** at entry 1, absent, nearest candidate 0.09
  degrees away in aim but not the same decision.

**Row 1 is refuted.** No class comes near 15,925 ms and no class hits more than
2 of 11 entries. The class-125 decision sequence is not available whole from
any other start, so it is not the chain explorer's first plan and not a row-2
reference for anything but its own class.

**The roots do return, and still find nothing.** Classes 100 and 150 attempt
entries 8, 9 and 10 late in their own legs - the bot passes within 64 units of
those roots - and no candidate matches. So the trip leaves the class-125
corridor rather than merely running late along it.

**Forcing the reachable prefix is worth time at two classes.** Taking the first
one or two class-125 decisions where they are admissible is **450 ms faster at
class 100 and 550 ms at class 150**, three worlds of three each, and changes
nothing at class 75 where the second decision is refused. That is a diagnostic
result and not a policy: the tape is not a prepared plan, the gain is measured
under forced selection, and no gate row moves. It does say the ranking's own
choice at those first decisions is not the best one available from those
starts, which is the selection question the preference branch was parked on.

## Tests

`tools/test_movement_decision_tape.py`, 20 cases on hand-built logs: the map
and checksum binding, the release filter and the release frame, the aim carried
as printed, a decision being both hops, and refusals for an unknown kind, a
missing field or a commit line without `d1`. The regressions for defects 1 and
4 were added failing and then fixed; the failing regressions for 2, 3 and 5 are
the retained worlds themselves, which is what an engine-side matching defect
can offer. The whole Python suite is **702 tests, all passing**.

## Two things the instrument does not settle

**The tie rule fires often.** 7 of 11 entries had two or three matching
admitted candidates, including a state entry, so `matches=1` is not what a
passing self-control looks like here. The tie-break reproduced the control
exactly, which is the evidence that the duplicates are the same decision
offered twice, but it is evidence and not proof.

**`strafe_fixture_build.py` exits 0 on a failed build.** One rebuild compiled
the dedicated engine and failed the game DLL, and the script reported success;
only the missing hash manifest stopped the broken fixture being used. The guard
held, but the exit code is a trap and is worth repairing separately.

## What this does not establish

The instrument is verified against itself and nothing else. No transfer world
has been read yet, so neither reading of the class difference is drawn. Forced
decisions are a diagnostic, not a prepared-plan execution, and move no gate
row. A trip that completes under the tape says the decisions were admissible
from that start, not that an explorer will find them or that they are robust to
contention. One case, one leg, one map.
