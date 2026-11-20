"""Report what upstream changed in the code xenjk has ported, since the port's snapshot.

    python tools/upstream_watch.py                      # every docs/ports/*.json, against each checkout's origin
    python tools/upstream_watch.py --fetch              # git fetch the checkouts first (GitHub only; no game servers)
    python tools/upstream_watch.py docs/ports/base_enhanced.json --target HEAD

A manifest (docs/ports/<upstream>.json) names the local checkout, the snapshot commit the port was taken from (or the
last upstream commit reviewed since, upstream.reviewed), and per
feature the upstream files and functions it mirrors. For each feature whose mirrored C functions differ between the
snapshot and the target (comments and whitespace ignored) it prints those functions and the commits touching their
files, so a new release shows exactly which ports to revisit (--verbose: also features whose files alone moved). It never edits the checkouts
beyond an explicit --fetch, and never edits xenjk. Localhost-only applies to games; this talks
to git remotes only when asked.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORTS = ROOT / 'docs' / 'ports'


def git(checkout, *args, check=True):
    run = subprocess.run(['git', '-C', str(checkout), *args], capture_output=True, text=True,
                         encoding='utf-8', errors='replace')
    if check and run.returncode:
        raise SystemExit('git %s failed in %s: %s' % (' '.join(args), checkout, run.stderr.strip()))
    return run.stdout


def show(checkout, rev, path):
    run = subprocess.run(['git', '-C', str(checkout), 'show', '%s:%s' % (rev, path)], capture_output=True)
    return run.stdout.decode('utf-8', 'replace') if run.returncode == 0 else None


def function_body(source, name):
    """The text of C function `name` (signature through its closing brace), or None. A definition is a line that
    names the function followed by '(' and, before any ';', an opening brace."""
    if source is None:
        return None
    for m in re.finditer(r'^[^\n;{}#]*\b%s\s*\(' % re.escape(name), source, re.M):
        brace = source.find('{', m.end())
        semi = source.find(';', m.end())
        if brace < 0 or (0 <= semi < brace):
            continue          # a declaration or a call, not the definition
        depth, i = 0, brace
        while i < len(source):
            c = source[i]
            if c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    return source[m.start():i + 1]
            i += 1
    return None


def normal(text):
    return None if text is None else re.sub(r'\s+', ' ', re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)).strip()


def features_of(manifest):
    """Accept a few shapes: a list under 'features' (or 'ports'), each with upstream file and function lists under
    any of the obvious keys."""
    items = manifest.get('features') or manifest.get('ports') or []
    for f in items:
        up = f.get('upstream') if isinstance(f.get('upstream'), dict) else f
        files = up.get('files') or up.get('upstream_files') or f.get('upstream_files') or []
        funcs = up.get('functions') or up.get('upstream_functions') or f.get('upstream_functions') or []
        if isinstance(files, str):
            files = [files]
        if isinstance(funcs, str):
            funcs = [funcs]
        # "file:function" entries are allowed in either list
        pairs = []
        for fn in funcs:
            if isinstance(fn, dict):            # {"file": ..., "function": ...}
                pairs.append((fn.get('file'), fn.get('function')))
                files.append(fn.get('file'))
            elif ':' in fn:
                path, name = fn.rsplit(':', 1)
                pairs.append((path, name))
            else:
                pairs.extend((p, fn) for p in files)
        yield f.get('name', '?'), f.get('status', ''), [x for x in dict.fromkeys(files) if x], pairs


def watch(path, target, fetch, verbose=False):
    manifest = json.loads(Path(path).read_text(encoding='utf-8'))
    up = manifest.get('upstream', manifest)
    checkout = up.get('checkout') or up.get('local_checkout') or ''
    checkout = (ROOT / checkout).resolve() if checkout else Path()   # relative to the repository root
    snapshot = up.get('snapshot') or up.get('snapshot_commit') or manifest.get('snapshot')
    reviewed = (up.get('reviewed') or {}).get('commit') if isinstance(up.get('reviewed'), dict) else up.get('reviewed')
    if reviewed:
        snapshot = reviewed      # upstream commits already looked at are not reported again
    if not checkout.is_dir() or not snapshot:
        raise SystemExit('%s: needs upstream.checkout (a local clone) and upstream.snapshot' % path)
    if fetch:
        print('fetching %s ...' % checkout, flush=True)
        git(checkout, 'fetch', '--quiet', 'origin')
    if target is None:
        target = 'origin/HEAD' if git(checkout, 'rev-parse', '--verify', '--quiet', 'origin/HEAD', check=False).strip() else 'HEAD'
    head = git(checkout, 'rev-parse', '--short', target).strip()
    total = git(checkout, 'rev-list', '--count', '%s..%s' % (snapshot, target)).strip()
    print('== %s: %s, snapshot %s -> %s %s (%s new commits)' % (Path(path).name, checkout, snapshot, target, head, total))
    changed_any = False
    for name, status, files, pairs in features_of(manifest):
        commits = git(checkout, 'log', '--format=%h %ad %s', '--date=short', '%s..%s' % (snapshot, target), '--', *files).strip() if files else ''
        moved = []
        for file, func in pairs:
            a = normal(function_body(show(checkout, snapshot, file), func))
            b = normal(function_body(show(checkout, target, file), func))
            if a != b:
                moved.append('%s:%s %s' % (file, func, 'gone' if b is None else 'new' if a is None else 'changed'))
        if not moved and not (verbose and commits):
            continue    # the files moved but the mirrored functions did not: nothing to re-port
        changed_any = True
        print('\n  %s%s' % (name, ' [%s]' % status if status else ''))
        for line in moved:
            print('    function ' + line)
        for line in commits.splitlines()[:8]:
            print('    commit   ' + line)
    if not changed_any:
        print('  no tracked file or function changed')
    return changed_any


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('manifest', nargs='*', type=Path, help='manifests (default: docs/ports/*.json)')
    ap.add_argument('--target', help='upstream ref to compare against (default: origin/HEAD, else HEAD)')
    ap.add_argument('--fetch', action='store_true', help='git fetch each checkout first')
    ap.add_argument('--verbose', action='store_true', help='also list features whose files changed but functions did not')
    args = ap.parse_args(argv)
    manifests = args.manifest or sorted(PORTS.glob('*.json'))
    if not manifests:
        raise SystemExit('no manifests under %s' % PORTS)
    changed = [watch(m, args.target, args.fetch, args.verbose) for m in manifests]
    return 1 if any(changed) else 0


if __name__ == '__main__':
    sys.exit(main())
