import json, numpy as np
M={k:json.load(open(f'metrics_{k}.json')) for k in ['final','raw','bend1','sub1','double','125','30']}
R=lambda x:round(float(x),2)
out=[]
for k,v in M.items():
    out.append(f"{k}: disp {R(np.mean([x['disp_mean'] for x in v.values()]))} inside {R(np.mean([x['sd_inside_count_mean'] for x in v.values()]))} core {R(np.mean([x['core_inside_mean'] for x in v.values()]))} corepeak {R(max(x['core_inside_max'] for x in v.values()))} ms {R(np.mean([x['ms_mean'] for x in v.values()]))} p95 {R(np.mean([x['ms_p95'] for x in v.values()]))} handc {R(np.mean([x['hand_contacts_mean'] for x in v.values()]))}")
print('\n'.join(out))
keys=['disp_mean','disp_max_all_frames','hem_mean','puff','hem_drop','sd_pen_worst','sd_inside_count_mean','sd_inside_count_max','core_inside_mean','core_inside_max','kinematic_inside_mean','band_edge_max','band_edge_min','skin_edge_max','edge_stretch_max','fold_frac_max','insideout_frac_max','hand_contacts_mean','ms_mean','ms_p95']
print('clip',*keys,sep='\t')
for c,v in M['final'].items(): print(c,*[R(v[k]) for k in keys],sep='\t')
