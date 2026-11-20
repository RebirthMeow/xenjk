# Human level-2 runs: the upper route

Recorded 2026-09-24. The owner recorded four red-stand to blue-flag runs with
`tools/human_route_record.py` (session
`.codex-research/human-route-20260924T153631627402Z/`, recordings under
`server/base/bot2-route/`). Human client commands are 8 ms (`pmove_msec 8`);
bots send 25 ms commands outside climbs.

| Recording | Time | Powers | Force | Class |
| --- | --- | --- | --- | --- |
| `1790264278-2-c0` | **7,825 ms** | Speed throughout (active 4) | 50 throughout | level 2: every takeoff 303 u/s, no Force Jump |
| `1790264232-0-c0` | 8,225 ms | Speed; Speed + Levitation on 112 rows | 42-50 | level 3 (a Force Jump) |
| `1790264254-1-c0` | 12,700 ms | Speed | 50 | level 2 |
| `1790264300-3-c0` | 13,400 ms | Speed | 50 | level 2 |

Speed was already active when each run was armed, so the pool shows no
purchase inside the clock; the bot buys at release.

## The route

Every run leaves the stand **north** and stays on the **upper level**
(z 1,808-1,993) to the blue flag. The 7,825 ms run: (142, -1178) → (171, -687)
at 1.0 s → (384, 57) at 2.0 s → (552, 955) at 3.0 s → (967, 1840) at 4.0 s →
(1643, 2641) at 5.0 s → (2065, 3685) at 6.0 s → (2448, 4755) at 7.0 s; first
takeoff at 225 ms, 8 takeoffs, 712 ms on the ground, top speed 1,257 u/s.

The bot's level-2 route ([baseline](2026-09-24-level2-baseline.md)) leaves
**south** through a hairpin, first takes off at 3.5 s, drops to the lower
floor (z 1,368) through (881, -125) and (913, 560), climbs back at the end,
and spends about 5.7 s on the ground. The level-1 library's tapes go north on
the upper route (entry 1 roots near (180, -765, 1912)).

## Use

Diagnostic and target only; nothing of the recording is replayed or given to
the planner. The next explorer run
(`.codex-research/explorer-l2rb-chain-20260924-a.sh`) requires the state
edges of each class's level-1 tape, a route the bot found itself.
