"""Per-pass GPU time from a scene run with the `gputimers` profile (rend2 r_speeds 100, rd-vulkan r_gpuTimes 1).

    python tools/renderer_gpu_passes.py <scene run dir> [--candidate somaz-rend2-prof]

rend2 prints one line group per frame, "<block>: <ms>ms ...", into qconsole.log. Frames are attributed to a view
between the scene script's "BENCHVIEW <view> begin" echo and the cgame's "benchview <view>: N frames" reply.
Reports the median ms per block and view, across reps.
"""
import argparse
import re
import statistics
from pathlib import Path

BLOCK_RE = re.compile(r'((?:View \d+ [A-Za-z ]+?)|(?:Main: [A-Za-z +]+?)|(?:Post[A-Za-z :+]*?)|(?:Pass [A-Za-z0-9 ]+?)): ([0-9.]+)ms')
VKGPU_RE = re.compile(r'VKGPU ([0-9.]+) \|')
BEGIN_RE = re.compile(r'BENCHVIEW (\S+) begin')
END_RE = re.compile(r'benchview (\S+): \d+ frames')


def read_run(log):
    """{view: {block: [ms per frame]}} from one client log."""
    out, view = {}, None
    for line in log.read_text(encoding='utf-8', errors='replace').splitlines():
        m = BEGIN_RE.search(line)
        if m:
            view = m.group(1)
            continue
        if END_RE.search(line):
            view = None
            continue
        if view:
            m = VKGPU_RE.search(line)
            if m:   # rd-vulkan r_gpuTimes: the frame's first-to-last timestamp
                out.setdefault(view, {}).setdefault('Frame (first to last pass)', []).append(float(m.group(1)))
            for name, ms in BLOCK_RE.findall(line):
                out.setdefault(view, {}).setdefault(name.strip(), []).append(float(ms))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('run_dir')
    ap.add_argument('--candidate', default='somaz-rend2-prof')
    ap.add_argument('--map', default='*', help='one map of a multi-map run (default: every map, views merged by name)')
    args = ap.parse_args(argv)
    merged = {}
    for log in sorted(Path(args.run_dir).glob('%s__%s__*/home/*/qconsole.log' % (args.candidate, args.map))):
        for view, blocks in read_run(log).items():
            for name, vals in blocks.items():
                merged.setdefault(name, {}).setdefault(view, []).append(statistics.median(vals))
    if not merged:
        raise SystemExit('no timer lines found (was r_speeds 100 set, and is this the -prof build?)')
    views = sorted({v for per in merged.values() for v in per})
    print('| block | ' + ' | '.join(views) + ' | mean |')
    print('|---|' + '---:|' * (len(views) + 1))
    for name in merged:
        cells = [statistics.median(merged[name][v]) if v in merged[name] else None for v in views]
        vals = [c for c in cells if c is not None]
        print('| %s | %s | %.3f |' % (name, ' | '.join('%.3f' % c if c is not None else '' for c in cells),
                                      statistics.mean(vals)))


if __name__ == '__main__':
    main()
