# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
"""The baselines of a Witcher 3 work folder's steps, and when they go stale.

Several steps rewrite a work folder's placements.json, materials.json and meshes/index.json from a copy made on
their first run, <name>.pre-<step>.json (w3foliage --merge: 'foliage'; w3trees: 'trees'; w3merge --tag T: T), so a
re-run replaces that step's own output instead of adding it twice. A copy holds whatever the earlier steps had
written when it was made: once an earlier step runs again, a later step's copy holds that step's *old* output, and
starting from it would silently throw the new output away.

<work>/baselines.json records the order: a counter, the count at which each step's copy was made and the count of
each step's last write. A step whose copy was made before an earlier step's last write is refused (start()), naming
that step; rebase=True moves its copies aside (into <work>/stale-baselines/<time>/, never deleted) and copies the
current files instead. A new export into the folder (w3gltf.py, w3city.py: new_export()) sets every copy aside.
Copies made before this record existed are taken as the oldest (their staleness is not known)."""
import json
import os
import shutil
import time

STATE = 'baselines.json'
FILES = {'placements': 'placements.json', 'materials': 'materials.json', 'index': os.path.join('meshes', 'index.json')}


def pre_path(work, key, tag):
    """The copy of FILES[key] a step named tag starts from"""
    rel = FILES[key]
    return os.path.join(work, os.path.dirname(rel), '%s.pre-%s.json' % (os.path.basename(rel)[:-5], tag))


def _load(work):
    p = os.path.join(work, STATE)
    if os.path.exists(p):
        with open(p, encoding='utf-8') as f:
            return json.load(f)
    return {'seq': 0, 'baselines': {}, 'writes': {}}


def _save(work, st):
    p = os.path.join(work, STATE)
    with open(p + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(st, f, indent=1)
    os.replace(p + '.tmp', p)


def _aside(work, paths, why):
    """Move files under work into stale-baselines/<time>-<why>/ (same relative paths); that folder"""
    dest = os.path.join(work, 'stale-baselines', time.strftime('%Y%m%d-%H%M%S') + '-' + why)
    for p in paths:
        q = os.path.join(dest, os.path.relpath(p, work))
        os.makedirs(os.path.dirname(q), exist_ok=True)
        os.replace(p, q)
    return dest


def stale_because(st, tag):
    """The steps that wrote again after tag's copy was made, having made theirs before it"""
    b = st['baselines'].get(tag)
    if b is None:
        return []
    tags = set(st['baselines']) | set(st['writes'])
    return sorted(x for x in tags if x != tag and st['baselines'].get(x, 0) < b and st['writes'].get(x, -1) > b)


def start(work, tag, keys=tuple(FILES), rebase=False):
    """The paths of tag's copies of FILES[keys], made from the current files where missing; SystemExit when they are
    stale (see the module's docstring) unless rebase"""
    st = _load(work)
    pres = {k: pre_path(work, k, tag) for k in keys}
    have = [p for p in pres.values() if os.path.exists(p)]
    if have and tag not in st['baselines']:
        st['baselines'][tag] = 0
        print('w3work: %s: copies made before %s existed; taken as the oldest' % (tag, STATE), flush=True)
    elif have:
        why = stale_because(st, tag)
        if why:
            names = ', '.join(os.path.relpath(p, work) for p in have)
            if not rebase:
                raise SystemExit('w3work: %s: its copies (%s) were made before the last run of %s; starting from them '
                                 'would drop that run. Re-run with --rebase (the copies set aside, the current files '
                                 'copied again), or redo the steps from the export.' % (tag, names, ', '.join(why)))
            print('w3work: %s: copies set aside in %s (older than %s)' % (tag, _aside(work, have, 'rebase-' + tag),
                                                                         ', '.join(why)), flush=True)
            st['baselines'].pop(tag)
            have = []
    made = False
    for k, p in pres.items():
        if not os.path.exists(p):
            shutil.copy2(os.path.join(work, FILES[k]), p)
            made = True
    if made and not have:
        st['seq'] += 1
        st['baselines'][tag] = st['seq']
    _save(work, st)
    return pres


def wrote(work, tag):
    """Record that tag has written the work files; says which later steps' copies are now stale"""
    st = _load(work)
    st['seq'] += 1
    st['writes'][tag] = st['seq']
    _save(work, st)
    later = sorted(x for x in st['baselines'] if tag in stale_because(st, x))
    if later:
        print('w3work: %s ran again after %s were made from its earlier output: re-run them (with --rebase)'
              % (tag, ', '.join(later)), flush=True)


def new_export(work):
    """A new export was written into work: every step's copy (and the record) set aside"""
    old = [os.path.join(d, f) for d in (work, os.path.join(work, 'meshes')) if os.path.isdir(d)
           for f in os.listdir(d) if '.pre-' in f and f.endswith('.json')]
    if os.path.exists(os.path.join(work, STATE)):
        old.append(os.path.join(work, STATE))
    if old:
        print('w3work: a new export: %d step copies set aside in %s' % (len(old), _aside(work, old, 'export')),
              flush=True)
