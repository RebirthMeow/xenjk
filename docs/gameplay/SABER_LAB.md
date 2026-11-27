# Saber lab

The saber lab measures saber outcomes under exactly controlled inputs. It exists so that saber changes, the
tick-independent saber pass first of all ([TICK_INDEPENDENCE.md](TICK_INDEPENDENCE.md)), can be judged on numbers
instead of bot fights.

| Part | Path |
|---|---|
| Game side | `codemp/game/g_saberlab.c`, the `saberlab` server command |
| Runner and report | `tools/saber_lab.py` |
| Scenarios | `tools/fixtures/saberlab/scenarios.json` |

## What it does

**Fighters.** Two bots become fighters with no AI. Their bot think is replaced, and `G_RunClient` never re-runs
them.

**Commands.** The lab sends each fighter a usercmd every `cmd` ms (default 8, a 125 fps client). The commands go
in packets every `pkt` ms (default 8) with no latency. Packets are processed before the game frame, while
`level.time` is still the previous frame's, which is when a real client's packets are handled. Both fighters'
packets are interleaved in time order. So every sv_fps sees the same command stream; only the server's frame
changes. Everything after the command is production code: Pmove, animation, Ghoul2 collision, the saber pass and
`G_Damage`.

**Reps.** A run is `reps` repetitions of one scripted exchange. Each rep:
1. Resets both fighters: position, view, health, stance, Force saber levels, saber state, animation timers.
2. Reseeds the game's random streams from `seed + rep`.
3. Idles for `settle` ms.
4. Acts for `act` ms.

**Phase sweep.** The action start is shifted by `(rep × 13) mod jitter` ms (default 0-32). Over a run, swings start
at every phase of the server frame and of the 33 ms saber grid.

**What it reads.** Outcomes come from state, not from instrumented saber code:
- Damage from each fighter's health change per frame.
- Clash results from `saberMove` category transitions.
- Locks from `saberLockTime`.

## Running

```bash
python tools/strafe_fixture_build.py --out .codex-research/<build>
```

```bash
python tools/saber_lab.py --build .codex-research/<build> --selfcheck
```

```bash
python tools/saber_lab.py --build .codex-research/<build> --scenario clash_medium --reps 200
```

**Cells.** `--cells` takes `fps:tick[:r]` items. The default is `30:1,40:1,60:1,125:1,250:1,30:0,125:0`, where
tick is `g_tickSabers` and `r` runs that cell in real time.

**Acceleration.** Cells run accelerated by default, with sv_fps S and timescale T chosen so that
`(1000 / S) × T` is the cell's frame and `S / T` its nominal sv_fps (30 Hz is 90 × 3, 125 Hz is 1000 × 8). Code
that reads `sv_fps` therefore sees the nominal rate. A 200-rep cell takes about 1-3 minutes.

**Other options:**
- `--set key=value` overrides any scenario key.
- `--report <dir>` re-summarises collected CSVs.
- Four worlds run at once at most, on localhost ports from 29200, staged under `%LOCALAPPDATA%/xenjk-play` with
  results under `.codex-research/`.

### Self-check

Run `--selfcheck` after any change to the lab, the engine frame loop or the saber pass. It must pass before any
result is used:

| Check | Cells | Must hold |
|---|---|---|
| Replay | 30:1 twice | byte-identical rows |
| Tick switch at 30 Hz | 30:1 and 30:0 | identical rows; the saber pass is a no-op at 30 Hz |
| Acceleration | 30:1 accelerated and real time, 125:1 accelerated and real time | byte-identical rows |

All four passed on 2026-10-21. Before per-rep reseeding was added, the acceleration checks failed: map load and bot
spawn drew a different number of random numbers depending on engine pacing. Damage and timings matched, but random
clash outcomes did not.

## Scenario keys

Pass these as `saberlab start key=value ...`. Fighter keys take an `a.` or `b.` prefix. Fighter a stands at the
anchor facing b, and b stands `dist` away facing a.

| Key | Default | Meaning |
|---|---|---|
| `name` | run | CSV name, `saberlab/<name>.csv` in the server's homepath |
| `reps` | 100 | repetitions |
| `dist` | 64 | distance between the fighters |
| `settle` / `act` | 800 / 1500 | ms idle before, and ms recorded after, each rep's action start |
| `cmd` / `pkt` | 8 / 8 | client command and packet interval (vanilla 125 fps with cl_maxpackets 30: cmd 8, pkt 40) |
| `hp` | 999 | health for both. SP-style saber damage is about 25 × blade travel per trace, so one clean hit is often several hundred; 999 shows the whole swing, while `hp=100` gives kill rates |
| `jitter` | 33 | the action start sweeps 0..jitter-1 ms across reps |
| `seed` | 1 | rep r draws from `seed + r × 7919` |
| `anchor` / `yaw` | auto | the first `info_player_deathmatch` in entity order and the first clear 22.5° direction. The seed doesn't affect either, so every cell fights on the same floor |
| `trace` | -1 | log every command and frame of that rep (`SABERLAB_TRACE` in games.log), to find where two runs part |
| `x.client` | 0 / 1 | the bot's client number |
| `x.style` | medium | fast, medium, strong, desann, tavion, dual, staff (or the SS_ number) |
| `x.off` / `x.def` | 3 / 3 | saber offense and defense level |
| `x.act` | idle | `holster` (saber off, cannot block), `idle` (saber up, blocks), `swing` (attack held over the window), `mash` (attack toggled at `hz`), `throw` (alt-attack held over the window, with saber throw at rank 3) |
| `x.start` / `x.hold` | 0 / 0 | action window, ms after the act start |
| `x.fm` / `x.rm` / `x.up` | 0 | movement keys held through the window; they choose the attack direction |
| `x.hz` | 0 | mash presses per second |
| `x.yaw` | 0 | view yaw offset from facing the other fighter |

## Output

`.codex-research/saberlab-<scenario>-<stamp>/` holds:
- `run.json`: build, scenario, seed and cells.
- `results.json`: per-cell completion.
- One folder per cell, with the CSV, `games.log`, the console log and `lab.cfg`.
- `report.md` and `summary.json`.

The CSV starts with a `#` line that records every setting, followed by one row per rep:

| Column | Meaning |
|---|---|
| `jitter` | this rep's start shift |
| `frame_ms` | the server frame |
| `a_dmg` / `b_dmg` | damage a dealt to b, and b to a |
| `a_hits` / `b_hits` | frames in which that damage landed |
| `a_first` / `b_first` | ms from the action start to the first damaging frame (-1: none) |
| `lock_ms` | time in a saber lock |
| `passes` / `frames` | saber passes and server frames in the act window (passes / frames is 1 at 30 Hz or slower, about 0.26 at 125 Hz) |
| `a_disarm` / `b_disarm` | saber knocked away during the rep |
| `a_seq` / `b_seq` | the saberMove categories entered, in order |

The `a_seq` / `b_seq` tokens:

| Token | Category |
|---|---|
| `r` | ready |
| `S`, `A<n>`, `R` | attack start, attack (the move number), return |
| `T` | transition |
| `B` | bounce |
| `D` | deflected |
| `V` | reflected |
| `H` | broken parry |
| `K` | knockaway |
| `P` | parry |
| `L` | saber lock |

`report.md` puts every cell side by side:
- Mean damage per rep with a 95% interval.
- Hit rate and the share of reps entering each outcome, with 95% Wilson intervals.
- First-hit time.

Any value whose interval doesn't overlap the reference cell (30:1) is starred. A star is a pointer, not a test; for
a firm claim, use enough reps that the interval is narrow.

## Limits

- Both fighters use one bot model (`--bot`, default tavion), so hitboxes are the same in every cell. Other models
  can be checked with `--bot`.
- The lab sees outcomes, not causes. To find why two cells differ, trace one rep in each (`trace=N`) and diff the
  `SABERLAB_TRACE` lines.
- With SP-style damage (`d_saberSPStyleDamage 1`, the default), an idle defender blocks only when the sabers
  physically meet; the automatic block roll is in the non-SP path. So a defender who doesn't move their saber is
  hit like a holstered one (`block_medium` and `dummy_medium` agree).
- Movement during a swing is whatever the scripted keys produce. A scenario that moves fighters tests movement and
  sabers together.
