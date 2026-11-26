import re, sys
OPN = {'6': 'abs', '7': 'sat', '21': 'exp2', '23': 'log2', '24': 'sqrt', '25': 'rsqrt', '27': 'floor', '28': 'ceil', '29': 'trunc', '35': 'max', '36': 'min', '22': 'frc', '13': 'sin', '12': 'cos'}
s = open(sys.argv[1]).read()
s = re.sub(r'call float @dx\.op\.(unary|binary)\.f32\(i32 (\d+), ', lambda k: 'call float @dx.op.' + OPN.get(k.group(2), k.group(1) + k.group(2)) + '.f32(i32 0, ', s)
open(sys.argv[2], 'w').write(s)
