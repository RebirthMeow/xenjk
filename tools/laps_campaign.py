"""Run laps-world arms in rounds and stop once each is decided against the baseline.

    python tools/laps_campaign.py --out .codex-research/<dir> --build .codex-research/<build> \\
        --arm base: --arm absorb:"--set bot_runnerAbsorb=1" [--baseline base] \\
        [--common "<defense_laps.py args>"] [--round 4] [--min 8] [--max 32] [--parallel 4] [--rate 25] [--primary team_caps]

Each arm is name:extra-args for tools/defense_laps.py (an arm may name its own build
with --build inside its args). Every round adds --round worlds to every arm that is
still undecided, up to --parallel worlds at once (AGENTS.md: at most four native
worlds), each on its own port; after the round, tools/laps_compare.py judges
every arm against the baseline on --primary. An arm stops when it is decided (an
effect at 2.5 standard errors, or a 95% interval inside +-tolerance) after at least
--min worlds, or at --max. The baseline keeps running while any arm runs, so both
sides of each comparison grow together. Writes <out>/<arm>-<n>/ worlds, campaign.log
and compare.txt; nothing is deleted. Localhost only.

The default --common is the milestone-B camper configuration.
Detach it for long runs (a Bash call stops at ten minutes):
    powershell -NoProfile -Command "Start-Process -WindowStyle Hidden -FilePath python -ArgumentList 'tools/laps_campaign.py ...'"
"""
import argparse
import os
import queue
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import laps_compare as lc  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
COMMON = ('--camper --defender-rearm --defender-reset --defender-mines 2 --chaser-ambush --set bot_defenderWeapons=8a,11,10a '
          '--set bot_chaserGoal=1 --set bot_defenderCarryLog=1 --set g_fixGolanDamage=1 --set bot_defender=2')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--build', required=True)
    ap.add_argument('--arm', action='append', required=True, metavar='NAME:ARGS')
    ap.add_argument('--baseline', help='arm name (default: the first --arm)')
    ap.add_argument('--common', default=COMMON)
    ap.add_argument('--map', default='mp/ctf_kejim')
    ap.add_argument('--game-seconds', type=float, default=600)
    ap.add_argument('--rate', type=float, default=25)
    ap.add_argument('--round', type=int, default=4)
    ap.add_argument('--min', type=int, default=8)
    ap.add_argument('--max', type=int, default=32)
    ap.add_argument('--parallel', type=int, default=4)
    ap.add_argument('--port', type=int, default=29700, help='first of --parallel consecutive ports, each reused only after its world exits')
    ap.add_argument('--primary', default='team_caps')
    ap.add_argument('--tolerance', type=float, default=.15)
    args = ap.parse_args()
    if args.parallel > 4:
        raise SystemExit('at most four native worlds at once (AGENTS.md)')
    arms = {}
    for spec in args.arm:
        name, _, extra = spec.partition(':')
        arms[name] = extra
    base = args.baseline or next(iter(arms))
    args.out.mkdir(parents=True, exist_ok=True)
    log = (args.out / 'campaign.log').open('a', encoding='utf-8')

    def say(text):
        line = '%s %s' % (time.strftime('%H:%M:%S'), text)
        print(line, flush=True)
        log.write(line + '\n')
        log.flush()

    say('START arms=%s baseline=%s rate=%g round=%d min=%d max=%d parallel=%d' % (
        list(arms), base, args.rate, args.round, args.min, args.max, args.parallel))
    count = {name: len(lc.worlds(str(args.out / name))) for name in arms}
    active = set(arms)

    ports = queue.Queue()
    for k in range(args.parallel):
        ports.put(args.port + k)

    def run(job):
        name, n = job
        port = ports.get()   # a port is reused only after its last world has exited
        try:
            out = args.out / ('%s-%d' % (name, n))
            extra = shlex.split(arms[name])
            build = [] if '--build' in extra else ['--build', args.build]
            cmd = [sys.executable, str(ROOT / 'tools/defense_laps.py'), *build, '--out', str(out), '--map', args.map,
                   '--game-seconds', str(args.game_seconds), '--rate', str(args.rate), '--port', str(port),
                   *shlex.split(args.common), *extra]
            with open(str(out) + '.out.txt', 'w', encoding='utf-8') as fh:
                code = subprocess.call(cmd, cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT)
        finally:
            ports.put(port)
        say('%s-%d exit=%d' % (name, n, code))
        return code

    def used(name, n):
        world = args.out / ('%s-%d' % (name, n))
        return world.exists() or Path(str(world) + '.out.txt').exists()

    attempts = dict.fromkeys(list(arms) + [base], 0)
    while active:
        jobs = []
        for _ in range(args.round):   # interleaved, so a stopped campaign leaves the arms balanced
            for name in sorted(active | {base}):
                count[name] += 1
                # Never a number already used: a resumed campaign counts finished worlds only, and a failed world's
                # folder or log would be reused - its log truncated (logs stay put).
                while used(name, count[name]):
                    count[name] += 1
                jobs.append((name, count[name]))
                attempts[name] += 1
        with ThreadPoolExecutor(args.parallel) as pool:
            list(pool.map(run, jobs))
        others = [a for a in arms if a != base]
        text, verdicts = lc.report(lc.arm(str(args.out / base)), [lc.arm(str(args.out / a)) for a in others],
                                   args.primary, args.tolerance)
        (args.out / 'compare.txt').write_text(text + '\n', encoding='utf-8')
        for a in others:
            v = verdicts[lc.arm(str(args.out / a))['name']]
            done = (v['verdict'] != 'undecided' and v['n'] >= args.min) or v['n'] >= args.max
            # An arm whose worlds keep failing never reaches --max on finished worlds: stop it after twice as many
            # attempts (it would otherwise loop without end).
            if not done and attempts[a] >= 2 * args.max:
                say('ROUND %s gave up: %d attempts, %d finished worlds' % (a, attempts[a], v['n']))
                done = True
            say('ROUND %s n=%d %s %s %+.2f +- %.2f%s' % (a, v['n'], v['verdict'], args.primary, v['diff'], v['se'],
                                                       ' STOP' if done else ''))
            if done:
                active.discard(a)
        active.discard(base)
        if not others:
            break
    say('ALL_DONE')


if __name__ == '__main__':
    main()
