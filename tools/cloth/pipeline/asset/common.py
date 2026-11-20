"""Shared loading for the Eve 2B cloth asset: sim <-> Eve <-> glm matching. All paths read-only inputs.
Environment: SKIRT_CLOTH_LAB (the skirt lab checkout whose Out/chaosref/hare.skd is the Unreal trace; no default)."""
import os
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import json, sys, numpy as np
from scipy.spatial import cKDTree
import skdio, glaio
W = str(REPO / '.codex-research' / 'eve2b-glm-20261012') + '/'      # Eve source mesh + bone_mapping (unchanged)
NP = str(REPO / '.codex-research' / 'eve2b-nativeprop-20261012') + '/'  # eve2b_np: glm, transform.json, build_report


def skd_path():
    """the Unreal skirt trace, Out/chaosref/hare.skd under SKIRT_CLOTH_LAB"""
    lab = os.environ.get('SKIRT_CLOTH_LAB')
    if not lab:
        raise SystemExit('set SKIRT_CLOTH_LAB to the skirt lab folder (Out/chaosref/hare.skd)')
    return str(Path(lab) / 'Out' / 'chaosref' / 'hare.skd')


GLM = NP + 'build/lite/model.glm'
S_GLM = 37.5                    # glm units per metre
CM_PER_UNIT = 100.0 / S_GLM     # 2.6667 cm per glm unit
SKIRT_SURFS = ['skirt_0', 'skirt_1', 'skirt_2', 'skirt_3']

def load_all():
    z = np.load(W + 'eve_mesh.npz'); P = z['pos'].astype(np.float64)
    meta = json.load(open(W + 'eve_mesh.npz.json', encoding='utf-8'))
    tr = json.load(open(NP + 'build/lite/transform.json', encoding='utf-8'))
    bm = json.load(open(NP + 'build/lite/bone_mapping.json', encoding='utf-8'))
    d = skdio.read_skd(skd_path())
    glm = glaio.jka.read_glm(GLM)
    return dict(eve=P, evemeta=meta, tr=tr, bm=bm, skd=d, glm=glm)

def sim_to_eve(A):
    """index of the Eve vertex of every sim vertex (UE cm -> Blender m is (x,-y,z)/100), plus match distance (cm)."""
    P, tr, d = A['eve'], A['tr'], A['skd']
    cand = np.array(sorted(set(i for s in tr['skirt_surfaces'][:4] for i in s['original_vertex_indices'])))
    t = cKDTree(P[cand] * 100 * np.array([1, -1, 1]))
    dist, ix = t.query(d['pos'].astype(np.float64))
    return cand[ix], dist

def skirt_glm_verts(A):
    """per skirt surface: positions (n,3) glm units, bone index (global gla) and weights (n,4), original Eve index"""
    glm, tr = A['glm'], A['tr']
    out = []
    names = [s['name'] for s in glm['surfaces']]
    for si, nm in enumerate(SKIRT_SURFS):
        k = names.index(nm); sf = glm['lods'][0][k]
        ts = tr['skirt_surfaces'][si]; assert ts['surface'] == nm
        n = len(sf['verts']); assert n == ts['glm_vertex_count']
        pos = np.array([v['co'] for v in sf['verts']], dtype=np.float64)
        bi = np.full((n, 4), -1, dtype=np.int32); wt = np.zeros((n, 4))
        for i, v in enumerate(sf['verts']):
            for j, (b, w) in enumerate(zip(v['b'], v['w'])):
                bi[i, j] = sf['brefs'][b]; wt[i, j] = w
        out.append(dict(name=nm, k=k, pos=pos, bone=bi, w=wt, eve=np.array(ts['original_vertex_indices']), tris=np.array(sf['tris']), uv=np.array([v['uv'] for v in sf['verts']])))
    return out
