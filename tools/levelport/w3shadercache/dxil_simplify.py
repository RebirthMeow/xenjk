"""Condense dxc -dumpbin LLVM IR into short lines: cbuffer loads as cb<reg>.<comp>, samples, math ops."""
import re, sys
txt = open(sys.argv[1]).read()
body = txt[txt.index('define void @'):]
body = body[:body.index('\n}\n')]
names = {}
out = []
cbret = {}
for line in body.splitlines()[1:]:
    line = line.split(';')[0].strip()
    if not line: continue
    m = re.match(r'(%\w+) = (.*)', line)
    lhs, rhs = (m.group(1), m.group(2)) if m else (None, line)
    def r(s):
        return re.sub(r'%\w+', lambda k: names.get(k.group(), k.group()), s)
    if 'createHandle' in rhs or 'annotateHandle' in rhs:
        continue
    mm = re.search(r'cbufferLoadLegacy\.f32\(i32 59, %dx.types.Handle (%\w+), i32 (\d+)\)', rhs)
    if mm:
        cbret[lhs] = 'CB%s[%s]' % (mm.group(1), mm.group(2)); continue
    mm = re.match(r'extractvalue %dx\.types\.CBufRet\.f32 (%\w+), (\d)', rhs)
    if mm and mm.group(1) in cbret:
        names[lhs] = cbret[mm.group(1)] + '.' + 'xyzw'[int(mm.group(2))]; continue
    mm = re.search(r'loadInput\.\w+\(i32 4, i32 (\d+), i32 0, i8 (\d)', rhs)
    if mm:
        names[lhs] = 'IN%s.%s' % (mm.group(1), 'xyzw'[int(mm.group(2))]); continue
    rhs2 = re.sub(r'call (float|i32|%dx\.types\.\w+(\.f32)?|void) @dx\.op\.(\w+)(\.\w+)?\(i32 \d+, ', r'\3(', rhs)
    rhs2 = re.sub(r'%dx\.types\.Handle |float |i32 |i1 |<\d+ x float> ', '', rhs2)
    rhs2 = re.sub(r'fast |reassoc |nnan |ninf |nsz |arcp |afn |contract ', '', rhs2)
    out.append('%s = %s' % (lhs, r(rhs2)) if lhs else r(rhs2))
print('\n'.join(out))
