"""Turn the pk3 copies that look runs staged into their homes into hard links to the build outputs they were copied
from, so each build's bytes are stored once; nothing is deleted. A copy matches an original when the name, size and
modification time agree (shutil.copy2 keeps the time) and sampled chunks compare equal; before a copy is replaced by
the link the whole files are compared. A copy with no matching original (its build rebuilt or gone, or a copy cut
short) is moved to .codex-trash/<stamp>-staged-pk3/ by research_shed's Shed (MANIFEST.tsv row written before each
move), for a person to empty or restore with `research_shed.py --restore <stamp>-staged-pk3`.

    python tools/dedupe_staged_pk3.py              # dry run: matched / unmatched counts and bytes
    python tools/dedupe_staged_pk3.py --apply
"""
import argparse, filecmp, os, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from research_shed import Shed  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
RESEARCH = ROOT / '.codex-research'
TRASH = ROOT / '.codex-trash'
LP = RESEARCH / 'levelport'
CHUNK = 1 << 20


def same_bytes(a, b, size):
    offs = sorted({0, max(size - CHUNK, 0)} | {size * k // 9 for k in range(1, 9)})
    with open(a, 'rb') as fa, open(b, 'rb') as fb:
        for o in offs:
            fa.seek(o)
            fb.seek(o)
            if fa.read(CHUNK) != fb.read(CHUNK):
                return False
    return True


def volume(p):
    return os.path.splitdrive(os.path.realpath(p))[0].lower()


def link_copy(match, p):
    """replace copy p by a hard link to match; False (p untouched) when that cannot be done"""
    if not filecmp.cmp(match, p, shallow=False):
        print('skip %s: differs from %s outside the sampled chunks' % (p, match), file=sys.stderr)
        return False
    tmp = p.with_name(p.name + '.link')
    try:
        if os.path.lexists(tmp):
            os.remove(tmp)      # a link an interrupted run left: it is a second name of the original, not data
        os.link(match, tmp)
        os.replace(tmp, p)
    except OSError as e:        # a client holds the copy open: leave it, the rest of the run continues
        print('skip %s: %s' % (p, e), file=sys.stderr)
        if os.path.lexists(tmp):
            os.remove(tmp)
        return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--look', default=str(LP / 'look'))
    ap.add_argument('--builds', default=str(LP / 'build'))
    a = ap.parse_args()
    look = Path(os.path.abspath(a.look))
    if os.path.relpath(look, RESEARCH).startswith('..'):
        raise SystemExit('--look must be under %s (the trash manifest is relative to it)' % RESEARCH)

    originals = {}
    for p in Path(a.builds).rglob('*.pk3'):
        st = p.stat()
        originals.setdefault((p.name, st.st_size), []).append((p, st))
    shed = Shed(RESEARCH, TRASH, a.apply)
    shed.dest = shed.dest.with_name(shed.dest.name + '-staged-pk3')
    linked = moved = 0
    linked_b = moved_b = 0
    unmatched = []
    for p in sorted(look.rglob('*.pk3')):
        st = p.stat()
        if st.st_nlink > 1:
            continue	# already a link
        match = None
        for o, ost in originals.get((p.name, st.st_size), []):
            if os.path.samefile(o, p):
                continue    # the original itself (--look overlapping --builds): not a copy of anything
            if abs(ost.st_mtime - st.st_mtime) < 2.0 and same_bytes(o, p, st.st_size):
                match = o
                break
        if match is None:
            if not any(os.path.samefile(o, p) for o, _ in originals.get((p.name, st.st_size), [])):
                unmatched.append((p, st.st_size))
            continue
        if not a.apply or link_copy(match, p):
            linked += 1
            linked_b += st.st_size
    if a.apply and unmatched and volume(look) != volume(TRASH):
        raise SystemExit('%d unmatched copies not moved: %s and %s are on different volumes (moves must be renames)'
                         % (len(unmatched), look, TRASH))
    for p, size in unmatched:
        before = shed.count.get('staged-pk3', 0)
        shed.tryMove('staged-pk3', str(p), size)
        if shed.count.get('staged-pk3', 0) > before:
            moved += 1
            moved_b += size
    print('%s: %d copies linked to their build (%.1f GB freed), %d unmatched moved to trash (%.1f GB)%s' % (
        'applied' if a.apply else 'dry run', linked, linked_b / 1e9, moved, moved_b / 1e9,
        (' -> ' + str(shed.dest)) if a.apply and moved else ''))


if __name__ == '__main__':
    sys.exit(main())
