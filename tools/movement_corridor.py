"""Build a guided corridor from a human demonstration's geometry.

The corridor is the coarse polyline of a recorded route's contract marks and
nothing else: no commands, no timing, no velocities, no states. The bot's
searches use it only to decide which cells they may travel through, and
generate every command themselves from the live state (docs/movement, the
guided expressiveness diagnostic). Removing the corridor leaves the same
searches unguided.

    python tools/movement_corridor.py <route.json> --out <corridor.txt>

The header binds the file to the map and checksum the recording was made on
and names the source asset by hash, so a run can say which demonstration
guided it.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def marks(asset):
    """The contract marks' positions, in order, with the start ahead of them."""
    points = [list(asset['start']['origin'])]
    for mark in asset['contract']:
        points.append(list(mark['origin']))
    return points


def thin(points, spacing):
    """Drop points closer than ``spacing`` to the last kept one; keep the ends."""
    if spacing <= 0 or len(points) < 3:
        return list(points)
    kept = [points[0]]
    for point in points[1:-1]:
        if math.dist(point, kept[-1]) >= spacing:
            kept.append(point)
    kept.append(points[-1])
    return kept


def header(asset, source, sha, count, spacing):
    binding = asset['binding']
    return ('corridor 1 map=%s checksum=%d points=%d\n'
            '# source=%s source_sha256=%s recording=%s spacing=%g\n'
            '# geometry only: no commands, timing, velocity or state is carried\n'
            % (binding['map'], binding['map_checksum'], count,
               source, sha, binding['recording'], spacing))


def render(asset, source, sha, spacing):
    points = thin(marks(asset), spacing)
    body = ''.join('p %.3f %.3f %.3f\n' % tuple(point) for point in points)
    return header(asset, source, sha, len(points), spacing) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('asset', type=Path, help='a BOT_ROUTE2 .route.json built from a recording')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--spacing', type=float, default=0.0,
                        help='drop marks closer together than this many units (0 keeps every mark)')
    args = parser.parse_args()
    asset = json.loads(args.asset.read_text())
    if asset.get('format') != 'BOT_ROUTE2':
        raise SystemExit('not a BOT_ROUTE2 asset: ' + str(args.asset))
    if not asset.get('reference', {}).get('complete'):
        raise SystemExit('the recording did not complete; an incomplete run is not a corridor')
    sha = hashlib.sha256(args.asset.read_bytes()).hexdigest()
    text = render(asset, args.asset.name, sha, args.spacing)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding='utf-8', newline='\n')
    print('%s: %d points from %s (%d ms reference)'
          % (args.out, text.count('\np '), args.asset.name, asset['reference']['elapsed_ms']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
