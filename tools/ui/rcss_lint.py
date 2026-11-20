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
"""Lint xenui documents (RML and RCSS) before a game launch: properties RmlUi does not have, web CSS that fails silently
in RmlUi, tweens it does not know, nav targets that are missing, and features xenui's renderer does not run.

The known properties and tweens are read from the vendored RmlUi source (lib/rmlui), so the lint follows its version.
See docs/xenui-agents.md for the rules behind each message.

  python -I tools/ui/rcss_lint.py <file or folder> [...]     exit 1 when there is an error
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RMLUI = os.path.join(ROOT, 'lib', 'rmlui', 'Source', 'Core')

# web CSS that RmlUi does not have, with what to use instead
WEB = {
    'clip-path': 'no clip-path in RmlUi: use a transform (skewX) on the panel, or overflow plus border-radius',
    'background-image': 'use decorator: image(<file>) (or a gradient decorator)',
    'grid-template-columns': 'no CSS grid in RmlUi: use flex', 'grid-template-rows': 'no CSS grid in RmlUi: use flex',
    'grid-area': 'no CSS grid in RmlUi: use flex', 'grid-column': 'no CSS grid in RmlUi: use flex',
    'grid-row': 'no CSS grid in RmlUi: use flex', 'grid': 'no CSS grid in RmlUi: use flex',
    'mix-blend-mode': 'no blend modes on elements', 'text-shadow': 'use font-effect: shadow(...)',
    'outline': 'no outline: use a border or a decorator', 'object-fit': 'no object-fit: size the img element',
    'animation-name': 'use the animation shorthand', 'transition-property': 'use the transition shorthand',
    'transition-duration': 'use the transition shorthand', 'transition-timing-function': 'use the transition shorthand',
    'will-change': 'not needed in RmlUi', 'user-select': 'not in RmlUi',
}
# decorators RmlUi has that xenui's renderer does not run (docs/xenui-agents.md); its version 2 renderer runs the
# properties (box-shadow, filters, mask-image, the gradients)
NOT_YET_DECORATORS = {'shader': 'custom shader decorators have nothing to run in xenui'}
LAYOUT_PROPS = {'left', 'top', 'right', 'bottom', 'width', 'height', 'min-width', 'min-height', 'max-width', 'max-height',
                'margin', 'margin-left', 'margin-right', 'margin-top', 'margin-bottom', 'padding', 'padding-left',
                'padding-right', 'padding-top', 'padding-bottom', 'font-size', 'line-height', 'letter-spacing'}
WEB_TWEENS =('ease', 'ease-in', 'ease-out', 'ease-in-out', 'step-start', 'step-end')
NAV_KEYWORDS = {'auto', 'none', 'horizontal', 'vertical'}
AT_RULES = {'keyframes', 'media', 'spritesheet'}


def rmlui_names():
    spec = open(os.path.join(RMLUI, 'StyleSheetSpecification.cpp'), encoding='utf-8').read()
    props = set(re.findall(r'Register(?:Property|Shorthand)\([A-Za-z:]+, "([a-z-]+)"', spec))
    anim = open(os.path.join(RMLUI, 'PropertyParserAnimation.cpp'), encoding='utf-8').read()
    tweens = set(re.findall(r'\{"([a-z-]+)", \{Tween', anim))
    return props, tweens


def line_of(text, pos):
    return text.count('\n', 0, pos) + 1


def strip_comments(text):
    return re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)


def declarations(text, base=0):
    """(property, value, offset) for every declaration inside every { } block, nested blocks included."""
    for m in re.finditer(r'([A-Za-z-]+)\s*:\s*([^;{}]+?)\s*(?:;|(?=\}))', text):
        # skip selectors such as a:hover { ... } (a ':' outside a block): a declaration follows '{' or ';'
        before = text[:m.start()].rstrip()
        if before and before[-1] in '{;':
            yield m.group(1).lower(), m.group(2), base + m.start()


def lint_rcss(path, text, props, tweens, ids, out, base=0, src=None):
    src = src if src is not None else text
    clean = strip_comments(text)
    for m in re.finditer(r'@([a-z-]+)', clean):
        if m.group(1) not in AT_RULES:
            out.append((path, line_of(src, base + m.start()), 'error', f'@{m.group(1)} is not an RCSS rule (RmlUi has @keyframes, @media, @spritesheet)'))
    for name, value, pos in declarations(clean, base):
        ln = line_of(src, pos)
        v = value.strip()
        if name in WEB:
            out.append((path, ln, 'error', f'{name}: {WEB[name]}'))
            continue
        if name.startswith('--'):
            out.append((path, ln, 'error', f'{name}: no custom properties (var()) in RCSS'))
            continue
        if name not in props:
            out.append((path, ln, 'error', f'{name}: not an RmlUi property'))
            continue
        if name == 'display' and v in ('grid', 'inline-grid', 'contents'):
            out.append((path, ln, 'error', f'display: {v} is not in RmlUi (use flex or block)'))
        if name == 'decorator':
            for d, why in NOT_YET_DECORATORS.items():
                if re.search(r'(^|[\s,])' + re.escape(d) + r'\(', v):
                    out.append((path, ln, 'warning', f'decorator {d}: {why}'))
        if name in ('transition', 'animation'):
            for word in re.findall(r'[a-z][a-z-]*', re.sub(r'\([^)]*\)', '', v)):
                if word in WEB_TWEENS or word == 'cubic-bezier':
                    out.append((path, ln, 'error', f'{name}: "{word}" is a web easing; RmlUi tweens are e.g. cubic-out, back-out, elastic-out'))
            for word in re.findall(r'\b[a-z]+-(?:in-out|in|out)\b', v):
                if word not in tweens and word not in WEB_TWEENS:
                    out.append((path, ln, 'error', f'{name}: "{word}" is not an RmlUi tween'))
            if 'cubic-bezier(' in v or 'steps(' in v:
                out.append((path, ln, 'error', f'{name}: cubic-bezier()/steps() are web easings; use an RmlUi tween'))
            if name == 'transition':
                for word in re.findall(r'[a-z][a-z-]*', v):
                    if word in LAYOUT_PROPS:
                        out.append((path, ln, 'warning', f'transition on {word} lays the whole document out every frame it runs: animate transform'))
        if name.startswith('nav') and ids is not None:
            for target in re.findall(r'#([A-Za-z0-9_-]+)', v):
                if target not in ids:
                    out.append((path, ln, 'error', f'{name}: no element #{target} in the document'))
            for word in re.findall(r'(?<![#\w-])([a-z]+)\b', v):
                if word not in NAV_KEYWORDS:
                    out.append((path, ln, 'error', f'{name}: "{word}" (nav takes auto, none, horizontal, vertical or #id)'))
        if re.search(r'\bvar\(|\bcalc\(|\bclamp\(|\bmin\(|\bmax\(', v):
            out.append((path, ln, 'error', f'{name}: var()/calc()/clamp()/min()/max() are not in RCSS'))
        if re.search(r'-?\d*\.?\d+(vmin|vmax|ch|ex|cm|mm|in|pt|pc)\b', v):
            out.append((path, ln, 'error', f'{name}: unit not in RmlUi (use px, dp, em, rem, %, vw, vh)'))


def lint_rml(path, props, tweens, out, seen):
    src = open(path, encoding='utf-8').read()
    ids = set(re.findall(r'\bid\s*=\s*"([^"]+)"', src))
    for m in re.finditer(r'<style[^>]*>(.*?)</style>', src, flags=re.S):
        lint_rcss(path, m.group(1), props, tweens, ids, out, base=m.start(1), src=src)
    for m in re.finditer(r'\bstyle\s*=\s*"([^"]*)"', src):
        lint_rcss(path, '{' + m.group(1) + '}', props, tweens, ids, out, base=m.start(1) - 1, src=src)
    for m in re.finditer(r'\bon[a-z]+\s*=\s*"', src):
        out.append((path, line_of(src, m.start()), 'warning', 'inline on* attributes need an event listener instancer in xenui'))
    for m in re.finditer(r'<link[^>]*href\s*=\s*"([^"]+\.rcss)"', src):
        sheet = os.path.normpath(os.path.join(os.path.dirname(path), m.group(1)))
        if not os.path.isfile(sheet):
            out.append((path, line_of(src, m.start()), 'error', f'style sheet {m.group(1)} not found'))
            continue
        seen.add(sheet)
        text = open(sheet, encoding='utf-8').read()
        lint_rcss(sheet, text, props, tweens, ids, out)


def main(args):
    props, tweens = rmlui_names()
    files = []
    for a in args or ['.']:
        if os.path.isdir(a):
            for root, _, names in os.walk(a):
                files += [os.path.join(root, n) for n in names if n.endswith(('.rml', '.rcss'))]
        else:
            files.append(a)
    out, seen = [], set()
    for f in sorted(f for f in files if f.endswith('.rml')):
        lint_rml(f, props, tweens, out, seen)
    for f in sorted(f for f in files if f.endswith('.rcss')):
        if os.path.normpath(f) not in seen:	# a sheet no document links: nav targets cannot be checked
            lint_rcss(f, open(f, encoding='utf-8').read(), props, tweens, None, out)
    out = sorted(set(out))
    for path, ln, level, msg in out:
        print(f'{path}:{ln}: {level}: {msg}')
    errors = sum(1 for o in out if o[2] == 'error')
    print(f'{len(files)} files, {errors} errors, {len(out) - errors} warnings ({len(props)} RmlUi properties, {len(tweens)} tweens)')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
