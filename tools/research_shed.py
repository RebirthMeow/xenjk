"""Move regenerable bulk out of .codex-research into a trash folder; never delete.

    python tools/research_shed.py                 # dry run: what would move, by kind
    python tools/research_shed.py --apply         # move it to .codex-trash/<stamp>/
    python tools/research_shed.py --restore <stamp>

Kinds moved (outside kept campaigns): world graph snapshots (base/bot2-graph), demos
of bot-only worlds (base/demos; never a human session's - play-vs-runner, human-route,
kejim-play - whose play cannot be regenerated), binaries and PDBs (world copies, .staged-binaries), build trees
except the build's top-level log/manifest files, old-engine bot2_*.bin tables, and raw
sample-*.jsonl that have a compact-*.jsonl sibling. Logs, game logs, play samples,
CSV/JSON results, scripts, tapes, libraries and seeds are never touched.

Moves are renames on the same volume: instant, nothing copied, exactly reversible.
Each stamp folder holds MANIFEST.tsv (kind, bytes, original path relative to the
research root). Space returns only when a person empties .codex-trash; no tool does.

Kept whole: names or regexes listed in .codex-research/KEEP.txt (one per line, # comments),
and campaigns dated within --keep-days of today (by the YYYYMMDD in the name, else mtime).
"""
import argparse, datetime, os, re, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESEARCH = ROOT / '.codex-research'
TRASH = ROOT / '.codex-trash'
BIN_EXT = {'.dll', '.exe', '.pdb', '.ilk', '.exp', '.idb', '.obj', '.lib', '.tlog', '.iobj', '.ipdb'}
BUILD_KEEP_EXT = {'.log', '.json', '.txt', '.md'}
DATE = re.compile(r'(20\d{6})')
# Campaigns a person played in. Their demos are the play itself, not regenerable bulk.
HUMAN = re.compile(r'^(play-vs-runner|human-route|kejim-play)-')
# A build tree by name: "...-build-..." at a word boundary, not any name containing "build" (a replay world named
# "oldbuild-armon" is a campaign).
BUILD = re.compile(r'(^|-)build(-|$)')


def redirect(path):
    """A symlink or a Windows junction (os.walk descends junctions on Python 3.11 and earlier)."""
    try:
        st = os.lstat(long(path))
    except OSError:
        return True
    return os.path.islink(path) or bool(getattr(st, 'st_file_attributes', 0) & 0x400)


def long(p):
    p = os.path.abspath(p)
    return p if os.name != 'nt' or p.startswith('\\\\?\\') else '\\\\?\\' + p


def load_keep(research):
    names, patterns = set(), []
    keep = research / 'KEEP.txt'
    if keep.exists():
        for line in keep.read_text(encoding='utf-8').splitlines():
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            (patterns.append(re.compile(line[3:])) if line.startswith('re:') else names.add(line))
    return names, patterns


def recent(entry, keep_days, today):
    if keep_days <= 0:
        return False
    m = DATE.search(entry.name)
    if m:
        try:
            day = datetime.datetime.strptime(m.group(1), '%Y%m%d').date()
            return (today - day).days < keep_days
        except ValueError:
            pass
    return (time.time() - entry.stat(follow_symlinks=False).st_mtime) < keep_days * 86400


class Shed:
    def __init__(self, research, trash, apply):
        self.research, self.apply = research, apply
        self.dest = trash / time.strftime('%Y%m%dT%H%M%S')
        self.tally, self.count, self.rows = {}, {}, []

    def tree_size(self, p):
        total = 0
        for dp, _, fns in os.walk(long(p)):
            for f in fns:
                try:
                    total += os.lstat(os.path.join(dp, f)).st_size
                except OSError:
                    pass
        return total

    def move(self, kind, path, size=None):
        if size is None:
            size = self.tree_size(path) if os.path.isdir(long(path)) else os.lstat(long(path)).st_size
        rel = os.path.relpath(path, self.research)
        row = f'{kind}\t{size}\t{rel}\n'
        if self.apply:
            target = self.dest / rel
            os.makedirs(long(target.parent), exist_ok=True)
            # The row first: a crash between the two leaves a row whose item is still in place (the restore skips
            # it), never an item in the trash with no row.
            with open(self.dest / 'MANIFEST.tsv', 'a', encoding='utf-8') as m:
                m.write(row)
            os.rename(long(path), long(target))
        else:
            self.rows.append(row)
        # counted once moved: an item tryMove skipped is not in the totals
        self.tally[kind] = self.tally.get(kind, 0) + size
        self.count[kind] = self.count.get(kind, 0) + 1

    def tryMove(self, kind, path, size=None):
        try:
            self.move(kind, path, size)
        except OSError as e:     # a locked item: skip it, the rest of the run continues
            print(f'skip {path}: {e}', file=sys.stderr)

    def build(self, camp):
        for e in list(os.scandir(camp)):
            if redirect(e.path):
                continue
            if e.is_dir(follow_symlinks=False):
                self.tryMove('build-tree', e.path)
            elif os.path.splitext(e.name)[1].lower() not in BUILD_KEEP_EXT:
                self.tryMove('build-tree', e.path, e.stat(follow_symlinks=False).st_size)

    def campaign(self, camp, demos=True):
        for dp, dns, fns in os.walk(camp):
            parent = os.path.basename(dp).lower()
            for d in list(dns):
                low = d.lower()
                if redirect(os.path.join(dp, d)):
                    dns.remove(d)    # never into a link: its target is not this campaign's
                elif parent == 'base' and (low == 'bot2-graph' or (low == 'demos' and demos)):
                    self.tryMove('graph' if low == 'bot2-graph' else 'demos', os.path.join(dp, d))
                    dns.remove(d)
                elif low == '.staged-binaries':
                    self.tryMove('binary', os.path.join(dp, d))
                    dns.remove(d)
            names = {n.lower() for n in fns}
            for f in fns:
                low, p = f.lower(), os.path.join(dp, f)
                ext = os.path.splitext(low)[1]
                if ext in BIN_EXT:
                    kind = 'binary'
                elif ext == '.bin' and low.startswith('bot2_'):
                    kind = 'old-engine-bin'
                elif re.fullmatch(r'sample-\d+\.jsonl', low) and 'compact-' + low[7:] in names:
                    kind = 'raw-samples'
                else:
                    continue
                try:
                    self.move(kind, p, os.lstat(p).st_size)
                except OSError as e:
                    print(f'skip {p}: {e}', file=sys.stderr)


def restore(trash, research, stamp):
    src = trash / stamp
    rows = (src / 'MANIFEST.tsv').read_text(encoding='utf-8').splitlines()
    restored = 0
    for row in rows:
        rel = row.split('\t', 2)[2]
        target = research / rel
        if os.path.exists(long(target)):
            print(f'exists, left in trash: {rel}', file=sys.stderr)
            continue
        if not os.path.exists(long(src / rel)):
            print(f'not in trash (never moved, or moved since): {rel}', file=sys.stderr)
            continue
        os.makedirs(long(target.parent), exist_ok=True)
        os.rename(long(src / rel), long(target))
        restored += 1
    print(f'restored {restored} of {len(rows)} entries from {src}')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--apply', action='store_true', help='move to the trash (default: dry run)')
    ap.add_argument('--restore', metavar='STAMP', help='move a trash stamp back where it came from')
    ap.add_argument('--keep-days', type=int, default=2, help='keep campaigns dated within this many days (default 2)')
    ap.add_argument('--research', type=Path, default=RESEARCH)
    ap.add_argument('--trash', type=Path, default=TRASH)
    args = ap.parse_args(argv)
    if os.path.splitdrive(args.research.resolve())[0].lower() != os.path.splitdrive(args.trash.resolve())[0].lower():
        raise SystemExit('trash must be on the same volume as the research root (moves must be renames)')
    if args.restore:
        return restore(args.trash, args.research, args.restore)

    names, patterns = load_keep(args.research)
    today = datetime.date.today()
    shed = Shed(args.research, args.trash, args.apply)
    for e in sorted(os.scandir(args.research), key=lambda e: e.name):
        if not e.is_dir(follow_symlinks=False) or e.name.startswith(('.', '_')) or e.name in names \
                or any(p.search(e.name) for p in patterns) or recent(e, args.keep_days, today):
            continue
        if BUILD.search(e.name.lower()):
            shed.build(e.path)
        else:
            shed.campaign(e.path, demos=not HUMAN.match(e.name.lower()))
        if args.apply:
            print(e.name, flush=True)
    if not args.apply and shed.rows:
        args.trash.mkdir(exist_ok=True)
        (args.trash / f'dryrun-{shed.dest.name}.tsv').write_text(''.join(shed.rows), encoding='utf-8')
    G = 1 << 30
    print(('MOVED to ' + str(shed.dest)) if args.apply else 'DRY RUN (nothing moved)')
    for k in sorted(shed.tally, key=shed.tally.get, reverse=True):
        print(f'  {k:16s} {shed.tally[k] / G:8.2f} GB {shed.count[k]:10d}')
    print(f'  {"total":16s} {sum(shed.tally.values()) / G:8.2f} GB (logical; hard-linked binaries free less)')


if __name__ == '__main__':
    main()
