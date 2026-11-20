"""Build a decision-tape library: the tapes, and the index the bot selects from.

A tape is what one world committed (`tools/movement_decision_tape.py`). A
library is a directory of tapes plus one `index.txt`, bound to a map and a
checksum the way the graph cache is, which the engine reads once per map load
under `bot2-decisions/<map>_<checksum>/` when `bot_moveTapeLibrary` is set.

Each index row is the key a leg start forms, and the tape to run for it:

    key <leg-id> <cell> <phase> <file>

The leg-id is the objective the leg runs to, formed by the bot itself from its
team and its objective entity; the cell is the graph cell the runner stands in
at the leg start; the phase is `level.time` modulo 200 on that frame. This tool
does not and cannot compute those - they are the bot's, read from the engine's
own `[TAPE] select` line - so it takes them as given and checks only what it
can: that the tapes agree with each other on the map, the checksum and the
graph, that no two rows claim the same key, and that every row names a tape
that is there.

The graph is the third part of the binding and the one the map checksum cannot
carry. A tape names graph cells and graph edges by index, so it means nothing
on a graph with different contents - measured, a tape explored on
a research seed selects on the exact rung and hits zero of eleven
entries on the unseeded graph of the same map. Every tape carries the identity
of the graph it was recorded on (`graph=g1-...` in its header, from the
engine's `[GRAPH] map_load` line), the index carries the one they agree on,
and the engine refuses the whole library when that is not the graph it loaded.
A library of tapes written before the binding existed declares `graph=none`
and is accepted as unbound, which is what every earlier campaign replays under;
`--graph` declares one for such tapes rather than guessing.

Read-only with respect to the tapes: each is copied byte for byte.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

PHASE_MODULUS = 200
INDEX_VERSION = 1


def tape_binding(text):
    """The map, checksum and graph a tape header declares, or None.

    `graph` is the optional trailing field: a tape written before the graph
    binding existed has none and reports `none`, which the engine accepts as
    unbound.
    """
    head = text.split('\n', 1)[0].split()
    if len(head) < 4 or head[0] != 'decisiontape':
        return None
    fields = dict(token.partition('=')[::2] for token in head[2:] if '=' in token)
    if 'map' not in fields or 'checksum' not in fields:
        return None
    return fields['map'], fields['checksum'], fields.get('graph', 'none')


def parse_key(text):
    """``LEG:CELL:PHASE=file`` as the index row it becomes."""
    spec, sep, path = text.partition('=')
    if not sep:
        raise ValueError('a key is LEG:CELL:PHASE=<tape file>: ' + text)
    parts = spec.split(':')
    if len(parts) != 3:
        raise ValueError('a key is LEG:CELL:PHASE=<tape file>: ' + text)
    leg, cell, phase = parts[0], parts[1], parts[2]
    if not leg or len(leg) > 31 or any(c.isspace() for c in leg):
        raise ValueError('leg-id must be one token of at most 31 characters: ' + text)
    cell, phase = int(cell), int(phase)
    if cell < 0:
        raise ValueError('cell must not be negative: ' + text)
    if not 0 <= phase < PHASE_MODULUS:
        raise ValueError('phase must be in [0, %d): %s' % (PHASE_MODULUS, text))
    return dict(leg=leg, cell=cell, phase=phase, tape=Path(path))


def index_text(mapname, checksum, rows, graph='none'):
    """The index file, newest binding first, one key per row.

    `graph=` is the last header field, so an engine that predates the graph
    binding parses the four before it and ignores this one.
    """
    lines = ['decisionindex %d map=%s checksum=%s keys=%d graph=%s'
             % (INDEX_VERSION, mapname, checksum, len(rows), graph)]
    lines += ['key %s %d %d %s' % (r['leg'], r['cell'], r['phase'], r['tape'].name) for r in rows]
    return '\n'.join(lines) + '\n'


def library_name(mapname, checksum):
    return '%s_%s' % (mapname.replace('/', '_').replace('\\', '_'), checksum)


def build(rows, out, graph=None, force=False):
    """Write the library and return its directory and its manifest.

    `graph` declares the graph identity for tapes that carry none; it is
    refused when a tape already declares a different one, because the tape
    knows what it was recorded on and this tool does not.
    """
    if not rows:
        raise ValueError('a library needs at least one key')
    if len(rows) > 64:
        raise ValueError('the engine reads at most 64 keys')
    seen = set()
    bindings = set()
    for row in rows:
        key = (row['leg'], row['cell'], row['phase'])
        if key in seen:
            raise ValueError('two rows claim the same key: %s %d %d' % key)
        seen.add(key)
        if not row['tape'].is_file():
            raise ValueError('no such tape: %s' % row['tape'])
        binding = tape_binding(row['tape'].read_text(errors='replace'))
        if binding is None:
            raise ValueError('not a decision tape: %s' % row['tape'])
        bindings.add(binding)
    names = {}
    for row in rows:
        name = row['tape'].name
        if names.setdefault(name, row['tape'].resolve()) != row['tape'].resolve():
            raise ValueError('two different tapes are both named %s' % name)
    if len(bindings) != 1:
        raise ValueError('the tapes are bound to different maps, checksums or graphs: %s'
                         % sorted(bindings))
    mapname, checksum, tape_graph = bindings.pop()
    if graph and tape_graph not in ('none', graph):
        raise ValueError('the tapes declare graph %s and --graph says %s'
                         % (tape_graph, graph))
    graph = tape_graph if tape_graph != 'none' else (graph or 'none')
    directory = Path(out) / library_name(mapname, checksum)
    if (directory / 'index.txt').exists() and not force:   # a library worlds were staged from
        raise ValueError('a library already exists at %s; pass --force to replace it' % directory)
    directory.mkdir(parents=True, exist_ok=True)
    for tape in names.values():
        shutil.copy2(tape, directory / tape.name)
    index = directory / 'index.txt'
    # LF, because the engine reads the index a line at a time and the last
    # field of a row is a file name.
    index.write_text(index_text(mapname, checksum, rows, graph), newline='\n')
    manifest = dict(
        directory=str(directory), map=mapname, checksum=checksum, graph=graph,
        index_sha256=hashlib.sha256(index.read_bytes()).hexdigest(),
        keys=[dict(leg=r['leg'], cell=r['cell'], phase=r['phase'], file=r['tape'].name,
                   source=str(r['tape'].resolve()),
                   sha256=hashlib.sha256(r['tape'].read_bytes()).hexdigest()) for r in rows])
    return directory, manifest


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True,
                    help='parent directory; the library is written to '
                         '<out>/<map>_<checksum>/, the name the engine binds by')
    ap.add_argument('--key', action='append', default=[], metavar='LEG:CELL:PHASE=TAPE',
                    help='one index row: the leg-id, start cell and phase the bot forms, '
                         'and the tape to run for them. Repeatable.')
    ap.add_argument('--graph', metavar='g1-...',
                    help="the graph identity the tapes were recorded on, for tapes whose "
                         "header predates the field. Taken from the engine's [GRAPH] map_load "
                         "line. Refused when a tape declares a different one.")
    ap.add_argument('--force', action='store_true', help='replace an existing library of this map')
    args = ap.parse_args(argv)
    rows = [parse_key(text) for text in args.key]
    directory, manifest = build(rows, args.out, args.graph, args.force)
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2), newline='\n')
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
