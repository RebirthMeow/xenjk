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
"""stagekey.py -- a code key for one stage of a levelport tool: a hash of the code that stage can run, not of whole
files (a collision fix once rebuilt all 90 scene parts because cp77_tiles hashed all of cp2gltf.py
for both kinds of part).

  code_key('cp2gltf.py', ['stage_build'], entry='main')
  code_key('cp2gltf.py', ['stage_build.walk_level'], entry=None)     (a function defined inside another)

From the roots, every top-level name a reached definition mentions is followed (functions, classes, constants,
regexes, `global` names), within the file and into the local modules it imports (`import cp77mat`: the attributes
used, `cp77mat.Materials`; `from nier2gltf import headroom`: that name). A local module used other than through
attributes is taken whole. A nested root (outer.inner) follows the outer function's other nested definitions it names
as well (the values it closes over are the caller's to key). The entry (main: argument defaults decide what a stage
does) is hashed and followed, except into the other stage functions it dispatches to (top-level names starting with
stage_ or scene_ that are not roots).

What is hashed is each reached definition's syntax tree (ast.dump, no positions, docstrings dropped), in an order of
their own: comments, docstrings, blank lines, moves within the file and other stages' code leave the key alone.
Top-level imports and statements that are not definitions are always in. A name reached only through getattr or
globals() is missed: none of these tools do that (checked); keep it so, or add the name to the roots. A full
rebuild before each numbered kit stays the safety net."""
import ast
import hashlib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
_CACHE = {}
FUNCS = (ast.FunctionDef, ast.AsyncFunctionDef)


def _strip_docstrings(tree):
    for node in ast.walk(tree):
        if isinstance(node, FUNCS + (ast.ClassDef,)) and node.body:
            first = node.body[0]
            if isinstance(first, ast.Expr) and isinstance(first.value, ast.Constant) and isinstance(first.value.value, str):
                node.body = node.body[1:] or [ast.Pass()]
    return tree


class _Module:
    """One file's top-level definitions: name -> [statement indexes], the statements always in, the functions defined
    inside top-level ones (outer.inner -> node) and each top-level function's own imports."""

    def __init__(self, path):
        self.path = Path(path)
        tree = _strip_docstrings(ast.parse(self.path.read_text(encoding='utf-8')))
        self.defs, self.always, self.nested, self.inner_binds = {}, [], {}, {}
        for i, node in enumerate(tree.body):
            names = []
            if isinstance(node, FUNCS + (ast.ClassDef,)):
                names = [node.name]
                if isinstance(node, FUNCS):
                    for n in ast.walk(node):
                        if isinstance(n, FUNCS) and n is not node:
                            self.nested.setdefault('%s.%s' % (node.name, n.name), n)
                    self.inner_binds[node.name] = _names(node)[2]
            elif isinstance(node, (ast.Assign, ast.AnnAssign, ast.AugAssign)):
                targets = node.targets if isinstance(node, ast.Assign) else [node.target]
                names = [n.id for t in targets for n in ast.walk(t) if isinstance(n, ast.Name)]
            elif i == 0 and isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant):
                continue                                        # the module docstring
            elif isinstance(node, ast.If) and '__main__' in ast.dump(node.test):
                continue                                        # runs only as a script: the entry is named instead
            if names:
                for n in names:
                    self.defs.setdefault(n, []).append(i)
            else:
                self.always.append(i)
        self.body = tree.body
        self.dumps = {i: ast.dump(n) for i, n in enumerate(tree.body)}
        self.dumps.update({k: ast.dump(n) for k, n in self.nested.items()})

    def node(self, i):
        return self.nested[i] if isinstance(i, str) else self.body[i]


def _module(path):
    path = Path(path).resolve()
    st = path.stat()
    k = (str(path), st.st_size, st.st_mtime_ns)
    if k not in _CACHE:
        _CACHE[k] = _Module(path)
    return _CACHE[k]


def _local(name, base):
    f = base / (name + '.py')
    return f if f.exists() else None


def _names(node):
    """Names a statement mentions (with `global` ones), its `alias.attr` uses, and the imports it makes."""
    names, attrs, binds = set(), {}, {}
    for n in ast.walk(node):
        if isinstance(n, ast.Name):
            names.add(n.id)
        elif isinstance(n, ast.Global):
            names.update(n.names)
        elif isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name):
            attrs.setdefault(n.value.id, set()).add(n.attr)
        elif isinstance(n, ast.Import):
            for al in n.names:
                binds[al.asname or al.name.split('.')[0]] = (al.name, None)
        elif isinstance(n, ast.ImportFrom) and not n.level and n.module:
            for al in n.names:
                binds[al.asname or al.name] = (n.module, al.name)
    return names, attrs, binds


def _bare(node, alias):
    """Whether `alias` appears other than as `alias.attr` (passed on, called): then its module is taken whole."""
    for p in ast.walk(node):
        for c in ast.iter_child_nodes(p):
            if isinstance(c, ast.Name) and c.id == alias and not (isinstance(p, ast.Attribute) and p.value is c):
                return True
    return isinstance(node, ast.Name) and node.id == alias


def closure(path, roots, entry=None, base=None):
    """The (file, statement) pairs a stage reaches (a statement: its top-level index, or outer.inner), and the files
    taken whole."""
    base = Path(base) if base else HERE
    seen, whole, files = set(), set(), set()
    f0 = Path(path).resolve()
    todo = [(f0, r, False) for r in roots] + ([(f0, entry, True)] if entry else [])
    root_set = set(roots)

    def visit(f, mod, i, is_entry):
        if (f, i) not in seen:
            seen.add((f, i))
            _follow(mod, f, i, todo, base, root_set, is_entry)
    while todo:
        f, name, is_entry = todo.pop()
        if f in whole:
            continue
        mod = _module(f)
        if f not in files:                              # a file's imports and other top-level statements: always in
            files.add(f)
            for i in mod.always:
                visit(f, mod, i, False)
        if name is None:                                # the whole module
            whole.add(f)
            for i in range(len(mod.body)):
                visit(f, mod, i, False)
            continue
        if '.' in name:
            if name not in mod.nested:
                raise KeyError('stagekey: no %s in %s' % (name, f.name))
            visit(f, mod, name, False)
            continue
        for i in mod.defs.get(name, []):
            visit(f, mod, i, is_entry)
        if name not in mod.defs:                        # a root the file imports (from xscn import SceneImages)
            for j in mod.always:
                modname, imported = _names(mod.body[j])[2].get(name, (None, None))
                lf = _local(modname.split('.')[0], base) if modname and imported else None
                if lf is not None and lf.resolve() != f:
                    todo.append((lf.resolve(), imported, False))
    return seen, whole


def _follow(mod, f, i, todo, base, root_set, is_entry):
    node = mod.node(i)
    names, attrs, binds = _names(node)
    outer = i.split('.')[0] if isinstance(i, str) else None
    for n in names:
        if outer and '%s.%s' % (outer, n) in mod.nested:
            todo.append((f, '%s.%s' % (outer, n), False))    # a sibling defined in the same outer function
        elif n in mod.defs:
            if is_entry and (n.startswith('stage_') or n.startswith('scene_')) and n not in root_set:
                continue                                # the entry's dispatch to other stages
            todo.append((f, n, False))
    scope = {}
    for j in mod.always:                                # the file's top-level imports, the outer function's, its own
        scope.update(_names(mod.body[j])[2])
    if outer:
        scope.update(mod.inner_binds.get(outer, {}))
    scope.update(binds)
    for alias, (modname, imported) in scope.items():
        lf = _local(modname.split('.')[0], base)
        if lf is None or lf.resolve() == f or alias not in names | set(attrs):
            continue
        lf = lf.resolve()
        if imported is not None:
            if alias in names:
                todo.append((lf, imported, False))
        elif _bare(node, alias):
            todo.append((lf, None, False))
        else:
            for at in attrs.get(alias, ()):
                todo.append((lf, at, False))


def code_key(path, roots, entry='main', base=None, n=16):
    """The stage's code key: a hash of every reached statement's tree, sorted by file and tree (not position)."""
    path = Path(path) if Path(path).is_absolute() else (Path(base) if base else HERE) / path
    seen, _ = closure(path, roots, entry, base)
    h = hashlib.sha1(('py%d.%d\n' % sys.version_info[:2]).encode())
    for name, dump in sorted((f.name, _module(f).dumps[i]) for f, i in seen):
        h.update(('%s\n%s\n' % (name, dump)).encode())
    return h.hexdigest()[:n]


def report(path, roots, entry='main', base=None):
    """What a stage's key covers: per file, the names reached (for checking a key by eye)."""
    path = Path(path) if Path(path).is_absolute() else (Path(base) if base else HERE) / path
    seen, whole = closure(path, roots, entry, base)
    out = {}
    for f, i in seen:
        mod = _module(f)
        if isinstance(i, str):
            nm = i
        else:
            node = mod.body[i]
            nm = getattr(node, 'name', None) or ','.join(k for k, v in mod.defs.items() if i in v) or type(node).__name__
        out.setdefault(f.name, set()).add(nm)
    return {k: sorted(v) for k, v in sorted(out.items())}, sorted(f.name for f in whole)


if __name__ == '__main__':
    import json
    f, *roots = sys.argv[1:]
    print(code_key(f, roots))
    print(json.dumps(report(f, roots), indent=1))
