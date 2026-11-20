"""compare.py <old metrics.json> <new metrics.json> [out.md] -- per-clip table old -> new (RESULTS.md columns + band)."""
import sys, json
O = json.load(open(sys.argv[1])); N = json.load(open(sys.argv[2]))
cols = [('disp_mean', 1), ('disp_max_all_frames', 0), ('hem_mean', 1), ('sd_inside_count_mean', 1), ('sd_inside_count_max', 0),
        ('core_inside_mean', 2), ('core_inside_max', 0), ('fold_frac_max', 2), ('ms_mean', 2), ('band_edge_min', 2), ('band_edge_max', 2),
        ('kinematic_inside_mean', 1), ('sd_beyond_backdepth_mean', 1), ('hem_drop', 1), ('puff', 2)]
hdr = '| clip | ' + ' | '.join(c for c, _ in cols) + ' |'
lines = [hdr, '|' + '---|' * (len(cols) + 1)]
avg = {c: [0, 0] for c, _ in cols}
for clip in O:
    row = [clip]
    for c, d in cols:
        a, b = O[clip].get(c), N.get(clip, {}).get(c)
        if a is None or b is None: row.append('-'); continue
        avg[c][0] += a; avg[c][1] += b
        row.append(f'{a:.{d}f} / {b:.{d}f}')
    lines.append('| ' + ' | '.join(row) + ' |')
n = len(O)
lines.append('| **mean of clips** | ' + ' | '.join(f'{avg[c][0]/n:.{max(d,1)}f} / {avg[c][1]/n:.{max(d,1)}f}' for c, d in cols) + ' |')
txt = '\n'.join(lines)
print(txt)
if len(sys.argv) > 3: open(sys.argv[3], 'w', encoding='utf-8', newline='\n').write('old / new per cell\n\n' + txt + '\n')
