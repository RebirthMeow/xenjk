"""List cvars xenjk added since the Vulkan renderer arrived, from source registrations.

Three groups, each a diff of registered cvar names (Cvar_Get, Cvar_Register, XCVAR_DEF,
vmCvar tables) in codemp/:
  1. JKSunny's rd-vulkan as of upstream d4225a303, new against pre-import xenjk;
  2. our perf/vulkan commits in the JKSunny checkout (d4225a303..d4090e931, imported as e4fe7581);
  3. xenjk commits since the import (e4fe7581..HEAD).
Default, flags and description are read from the working tree.

    python tools/cvar_list.py > docs/xenjk-cvars.md
"""
import os, re, subprocess, sys
from collections import defaultdict

XENJK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JKSUNNY = os.environ.get("JKSUNNY_REPO", os.path.join(os.path.dirname(XENJK), "JKSunny-EternalJK"))
IMPORT, UPSTREAM, PERF_TIP = "e4fe7581", "d4225a303", "d4090e931"

CALL = re.compile(r'\b(?:Cvar_Get|Cvar_Register|XCVAR_DEF)\s*\(')
TABLE = re.compile(r'\{\s*&\s*\w+\s*,\s*"([^"]+)"\s*,\s*("[^"]*"|\w+)\s*,\s*(?:\w+\s*,\s*)?([A-Z_|\s0-9]+)\s*[,}]')
DESC = re.compile(r'Cvar_SetDescription\s*\(\s*(\w+)\s*,')
ASSIGN = re.compile(r'(\w+)\s*=\s*(?:ri\.|ri->)?Cvar_Get\s*\(\s*"([^"]+)"')
STR = re.compile(r'"((?:[^"\\]|\\.)*)"')


def git(repo, *a):
    return subprocess.run(["git", "-C", repo, *a], capture_output=True, text=True,
                          encoding="utf-8", errors="replace").stdout


def args_at(src, i):
    """Top-level comma-separated arguments of the call whose '(' is at src[i]."""
    depth, out, cur, j = 0, [], [], i
    while j < len(src):
        c = src[j]
        if c == '"':
            k = j + 1
            while k < len(src) and src[k] != '"':
                k += 2 if src[k] == '\\' else 1
            cur.append(src[j:k + 1]); j = k + 1; continue
        if c == '(':
            depth += 1
            if depth == 1: j += 1; continue
        elif c == ')':
            depth -= 1
            if depth == 0: out.append("".join(cur)); return out
        elif c == ',' and depth == 1:
            out.append("".join(cur)); cur = []; j += 1; continue
        cur.append(c); j += 1
    return out


def text(arg):
    s = STR.findall(arg)
    return "".join(s).replace('\\n', ' ').replace('\\"', '"') if s else arg.strip()


def scan(repo, ref, paths=("codemp",)):
    """Registrations at a commit, or in the working tree when ref is None."""
    listing = (git(repo, "ls-files", "--", *paths) if ref is None
               else git(repo, "ls-tree", "-r", "--name-only", ref, "--", *paths))
    files = [f for f in listing.splitlines() if re.search(r'\.(c|cpp|h)$', f)]
    # a name several renderers register takes rd-vulkan's default, flags and text (xenjk's renderer), not the first
    # renderer in path order (rd-rend2 sorts first)
    files.sort(key=lambda f: 0 if 'rd-vulkan/' in f else 1)
    out = {}
    for f in files:
        if ref is None:
            with open(os.path.join(repo, f), encoding="utf-8", errors="replace") as fh:
                src = fh.read()
        else:
            src = git(repo, "show", f"{ref}:{f}")
        if 'Cvar' not in src and 'XCVAR_DEF' not in src:
            continue
        found = []
        for m in CALL.finditer(src):
            line = src[src.rfind('\n', 0, m.start()) + 1:m.start()]
            if '#define' in line or line.lstrip().startswith(('//', '*')):
                continue
            a = args_at(src, m.end() - 1)
            # XCVAR_DEF tables and the modules' Cvar_Register have no description argument: the line's trailing comment
            rest = src[m.start():src.find('\n', m.start()) % (len(src) + 1)]
            c = re.search(r'\)\s*;?\s*//\s*(.*?)\s*$', rest)
            comment = c.group(1) if c else ""
            if m.group(0).startswith('Cvar_Register'):
                a = a[1:] + [comment]
            if m.group(0).startswith('XCVAR'):
                a = [a[0], a[1], a[3] if len(a) > 3 else "", comment]  # name, default, update, flags
            if len(a) < 3:
                continue
            name = a[0].strip().strip('"')
            if not re.fullmatch(r'[A-Za-z_]\w*', name):
                continue
            desc = a[3] if m.group(0).startswith(('XCVAR', 'Cvar_Register')) else (text(a[3]) if len(a) > 3 else "")
            found.append((name, text(a[1]), " ".join(a[2].split()), desc))
        for m in TABLE.finditer(src):
            found.append((m.group(1), m.group(2).strip('"'), " ".join(m.group(3).split()), ""))
        var2name = {v: n for v, n in ASSIGN.findall(src)}
        descs = {}
        for m in DESC.finditer(src):
            a = args_at(src, m.end() - 1)
            if len(a) > 1:
                descs[var2name.get(m.group(1), m.group(1))] = text(a[1])
        for name, d, fl, desc in found:
            e = out.setdefault(name.lower(), {"name": name, "default": d, "flags": fl, "desc": "", "files": set()})
            e["files"].add(f)
            e["desc"] = e["desc"] or desc or descs.get(name, "")
    return out


def area(name, files):
    n = name.lower()
    f = " ".join(files)
    for key, label in (("cl_video", "Video capture"), ("r_cloth", "Cloth"), ("r_hair", "Hair"),
                       ("r_charselfshadow", "Character self-shadow"), ("r_charshadow", "Character shadows"),
                       ("r_charcontact", "Character contact shadows"), ("r_charao", "Character AO"),
                       ("r_charocc", "Character occlusion pass"), ("r_char", "Character look"),
                       ("r_portraitrig", "Portrait rig (test lighting)"), ("r_cubemap", "Reflection probes"),
                       ("r_g2", "Ghoul2 animation"), ("r_ssao", "SSAO"), ("r_sun", "Sun and shadows"),
                       ("r_forcesun", "Sun and shadows"), ("r_shadow", "Sun and shadows"),
                       ("r_bloom", "Bloom"), ("r_flare", "Flares"), ("r_dlight", "Dynamic lights")):
        if n.startswith(key):
            return label
    if "rd-" in f:
        return "Renderer, other"
    return "Engine and client"


def table(rows, H):
    out = ["| cvar | default | flags | what |", "|---|---|---|---|"]
    for k in sorted(rows, key=str.lower):
        e = H[k]
        d = e["desc"].replace("|", "\\|").strip()
        out.append(f"| `{e['name']}` | `{e['default'] or ' '}` | {e['flags'].replace('|', '/') or '-'} | {d} |")
    return out


def main():
    for ref in (UPSTREAM, PERF_TIP):
        if not git(JKSUNNY, "rev-parse", "--verify", "--quiet", ref + "^{commit}").strip():
            raise SystemExit(f"{JKSUNNY} has no commit {ref}: set JKSUNNY_REPO to a JKSunny-EternalJK checkout")
    H = scan(XENJK, None)
    pre = scan(XENJK, IMPORT + "^")
    up = scan(JKSUNNY, UPSTREAM, ("codemp/rd-vulkan",))
    perf_tip = scan(JKSUNNY, PERF_TIP, ("codemp/rd-vulkan",))
    at_import = scan(XENJK, IMPORT)
    g1 = {k for k in up if k not in pre and k in H}
    g2 = {k for k in perf_tip if k not in up and k in H}
    g3 = {k for k in H if k not in at_import}
    head = git(XENJK, "rev-parse", "--short=8", "HEAD").strip()
    if git(XENJK, "status", "--porcelain", "--", "codemp").strip():
        head += " plus uncommitted changes"
    L = ["# xenjk cvars added since the Vulkan renderer", "",
         f"Generated by `tools/cvar_list.py` on {head}; regenerate rather than edit. Defaults, flags and "
         "descriptions are the current source's; `-` flags means none. Cvars that only changed default "
         "are not listed.", "",
         f"- [Vulkan renderer (JKSunny upstream)](#vulkan-renderer-jksunny-upstream): {len(g1)} cvars "
         "rd-vulkan brought that pre-Vulkan xenjk did not register.",
         f"- [perf/vulkan (ours, before the import)](#perfvulkan-ours-before-the-import): {len(g2)} cvars from our "
         f"commits in the JKSunny checkout ({UPSTREAM}..{PERF_TIP}); some reuse rend2 names.",
         f"- [Since the import (ours, in xenjk)](#since-the-import-ours-in-xenjk): {len(g3)} cvars from "
         f"{IMPORT}..{head}.", ""]
    L += ["## Vulkan renderer (JKSunny upstream)", ""] + table(g1, H) + [""]
    L += ["## perf/vulkan (ours, before the import)", ""] + table(g2, H) + [""]
    L += ["## Since the import (ours, in xenjk)", ""]
    groups = defaultdict(set)
    for k in g3:
        groups[area(H[k]["name"], H[k]["files"])].add(k)
    for label in sorted(groups):
        L += [f"### {label}", ""] + table(groups[label], H) + [""]
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    print("\n".join(L).rstrip() + "\n", end="")


if __name__ == "__main__":
    main()
