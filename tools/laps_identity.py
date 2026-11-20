"""Lossless check for two runs of the same seeded laps world (defense_laps.py --seed N).

An optimisation that changes nothing a bot decides must leave the world identical: the game log (but its shutdown
line, which is wall clock) and every decision line in the console ([MOVE], [ROUTE], [DEFENDER], [RUNNER], [GRAPH],
[SPIKE]) with the fields that are measurements of the machine, not of the game, removed (elapsed/rollout/think
milliseconds and microseconds, cache counters). Prints the first difference of each stream, exit 1 if any.

    python tools/laps_identity.py <world A> <world B> [--min-seconds 590] [--min-lines 10000]

Exit 0 identical, 1 differ, 2 not compared (a run missing, or short of the coverage minimums).

Kejim seeded worlds did not replay when a slow map load (four worlds at once) delayed the botat setup
(records/2026-10-09-kejim-replay.md); a server with the command-buffer fix (387e69e9) replays them. A world whose
setup ran late (summary setup_late_commands > 0) is not a replay of its seed.
"""
import argparse
import re
import sys
from pathlib import Path

# Every decision stream. [TAPE]'s other lines are the library's load and census.
DECISION = re.compile(r'\[(MOVE|ROUTE|DEFENDER|RUNNER|GRAPH|SPIKE|REROUTE|PLANGUIDE|PATHTABLE|SHADOW|FORECAST)\]|\[TAPE\] select ')
MACHINE = re.compile(r'\b(\w*elapsed_ms|\w*_us|\w*Us|rollout_ms|think_ms|lookahead_ms|wall\w*|traces|cache\w*|hits|misses|'
                     r'replayed\w*|search_ms|clock\w*)=\S+')
# A defender's hits= and misses= are throws and shots, a game count; only its timing fields are the machine's.
MACHINE_DEFENDER = re.compile(r'\b(\w*elapsed_ms|\w*_us|\w*Us|rollout_ms|think_ms|lookahead_ms|wall\w*|search_ms|clock\w*)=\S+')
PRELOAD_MS = re.compile(r'\bms=\S+')
LEVEL = re.compile(r'\blevel=(\d+)')
STAMP = re.compile(r'^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d ')


VERSION = re.compile(r'\\(version|gamename)\\[^\\]*')
GAMETIME = re.compile(r'^\s*(\d+):(\d\d) ')


def game(world):
    lines = (Path(world) / 'live/server/base/play-game.log').read_text(errors='replace').splitlines()
    # InitGame's \version\ and \gamename\ name the commit (and build date), a property of the binary, not the game
    return [VERSION.sub(r'\\\1\\*', l) for l in lines if 'ShutdownGame' not in l and not l.startswith('-----')]


def game_seconds(line):
    m = GAMETIME.match(line)
    return int(m[1]) * 60 + int(m[2]) if m else None


def decisions(world):
    out = []
    for l in (Path(world) / 'live/server-console.log').read_text(errors='replace').splitlines():
        l = STAMP.sub('', l)
        if DECISION.search(l) and '[FRAMECOST]' not in l and '[FRAMESLOW]' not in l:
            if l.startswith('[GRAPH] guide_preload'):   # its ms= is the load's wall clock (a bare ms= elsewhere is game data)
                l = PRELOAD_MS.sub('ms=*', l)
            out.append((MACHINE_DEFENDER if l.startswith('[DEFENDER]') else MACHINE).sub(r'\1=*', l).rstrip())
    return out


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, x, y
    if len(a) != len(b):
        i = min(len(a), len(b))
        return i, a[i] if i < len(a) else '<end>', b[i] if i < len(b) else '<end>'
    return None


def coverage(world, read_game, read_decisions):
    """(last whole game second in the game log, last level= ms of a decision line, decision line count)."""
    g = max((t for t in map(game_seconds, read_game) if t is not None), default=0)
    d = next((int(m[1]) for m in map(LEVEL.search, reversed(read_decisions)) if m), 0)
    return g, d, len(read_decisions)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('a')
    ap.add_argument('b')
    # Identity of two short or empty runs proves nothing: both runs must reach this much game time and decision
    # output before the comparison counts. A run that stopped early is reported as truncated (exit 2), never as
    # identical. The default suits defense_laps.py --game-seconds 600.
    ap.add_argument('--min-seconds', type=float, default=590, help='game seconds each run must reach (default 590)')
    ap.add_argument('--min-lines', type=int, default=10000, help='decision lines each run must have (default 10000)')
    args = ap.parse_args()
    a, b = args.a, args.b
    streams = {}
    truncated = 0
    for w in (a, b):
        try:
            streams[w] = (game(w), decisions(w))
        except OSError as e:
            print('%s: unreadable (%s)' % (w, e))
            truncated = 1
            continue
        g, d, n = coverage(w, *streams[w])
        short = [why for why, bad in (('game log ends at %d s' % g, g < args.min_seconds),
                                      ('decisions end at %.1f s' % (d / 1000), d / 1000 < args.min_seconds),
                                      ('%d decision lines' % n, n < args.min_lines)) if bad]
        print('%s: %s' % (w, 'TRUNCATED: ' + ', '.join(short) if short else
                          'complete (game log %d s, decisions %.1f s, %d lines)' % (g, d / 1000, n)))
        truncated |= bool(short)
    if truncated:
        print('not compared: a run is missing or ended before --min-seconds/--min-lines')
        sys.exit(2)
    bad = 0
    for i, name in enumerate(('game log', 'decision lines')):
        x, y = streams[a][i], streams[b][i]
        if name == 'game log':   # likewise for the game log: compare up to the last whole second both reached
            end = min(max((t for t in map(game_seconds, ls) if t is not None), default=0) for ls in (x, y))
            x, y = ([l for l in ls if game_seconds(l) is None or game_seconds(l) < end] for ls in (x, y))
        if name == 'decision lines':   # a server can run a few frames past 600 game s before it stops
            # the game time of each run's final decision line (a max over all lines would also pick up
            # level= fields that name a future time)
            last = lambda ls: next((int(m[1]) for m in map(LEVEL.search, reversed(ls)) if m), 0)
            end = min(last(x), last(y))
            # cut at the first line of the final common frame: a stopped server can end mid-frame, and some lines
            # print later than the frame they name, so 'the last line before the end' is not a clean cut
            x, y = (ls[:next((i for i, l in enumerate(ls) if LEVEL.search(l) and int(LEVEL.search(l)[1]) >= end), len(ls))]
                    for ls in (x, y))
        d = first_diff(x, y)
        if d is None:
            print('%s: identical (%d lines)' % (name, len(x)))
        else:
            bad = 1
            print('%s: DIFFER at line %d of %d/%d\n  A: %s\n  B: %s' % (name, d[0], len(x), len(y), d[1][:300], d[2][:300]))
    sys.exit(bad)


if __name__ == '__main__':
    main()
