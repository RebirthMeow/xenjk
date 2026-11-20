"""Extract a conservative, collision-unvalidated support inventory from Raven BSP and Detour data."""
import argparse, hashlib, json, math, re, struct, zipfile
from pathlib import Path

MSET = 0x4D534554
DNAV = 0x444E4156
NAV_VERSION = 7
METERS_TO_QUAKE = 1.0 / 0.0254
MAX_COUNT = 10_000_000


def sha(data): return hashlib.sha256(data).hexdigest()
def align4(n): return (n + 3) & ~3


def quake(v):
    """Recast X,Y(up),Z -> Quake X,Y,Z."""
    return [v[0] * METERS_TO_QUAKE, v[2] * METERS_TO_QUAKE, v[1] * METERS_TO_QUAKE]


def recast(v): return [v[0] / METERS_TO_QUAKE, v[2] / METERS_TO_QUAKE, v[1] / METERS_TO_QUAKE]


def _take(data, offset, size, what):
    if size < 0 or offset < 0 or offset + size > len(data):
        raise ValueError(f"truncated {what}")
    return data[offset:offset + size], offset + size


def _counts(values, names):
    out = dict(zip(names, values))
    if any(not 0 <= v <= MAX_COUNT for v in values): raise ValueError("invalid Detour count")
    return out


def parse_tile(data, tile_number=0):
    if len(data) < 100: raise ValueError("truncated Detour tile header")
    h = struct.unpack_from('<5iI9i10f', data, 0)
    if h[0] != DNAV or h[1] != NAV_VERSION: raise ValueError("unsupported Detour tile")
    c = _counts(h[6:15], ('poly','vert','link','detail_mesh','detail_vert','detail_tri','bv','offmesh','offmesh_base'))
    if c['offmesh_base'] > c['poly'] or c['offmesh'] > c['poly'] - c['offmesh_base']:
        raise ValueError("invalid offmesh range")
    floats=h[15:25]
    if not all(math.isfinite(v) for v in floats): raise ValueError('non-finite Detour tile header')
    if h[15]<=0 or h[16]<0 or h[17]<0 or any(h[18+i]>h[21+i] for i in range(3)):
        raise ValueError('invalid Detour walkability or bounds')
    sizes = [align4(100), align4(12*c['vert']), align4(32*c['poly']), align4(12*c['link']),
             align4(12*c['detail_mesh']), align4(12*c['detail_vert']), align4(4*c['detail_tri']),
             align4(16*c['bv']), align4(36*c['offmesh'])]
    if sum(sizes) != len(data): raise ValueError("Detour tile size/count mismatch")
    pos=sizes[0]
    verts_raw,_=_take(data,pos,sizes[1],'vertices'); pos+=sizes[1]
    verts=[struct.unpack_from('<3f',verts_raw,i*12) for i in range(c['vert'])]
    if any(not all(math.isfinite(x) for x in v) for v in verts): raise ValueError('non-finite Detour vertex')
    pos+=sizes[2]  # parsed below using its known start
    poly_start=sizes[0]+sizes[1]
    polygons=[]
    for i in range(c['poly']):
        p=struct.unpack_from('<I6H6HHBB',data,poly_start+i*32)
        n=p[14]; area_type=p[15]
        ptype=area_type>>6
        if ptype not in (0,1): raise ValueError('unknown Detour polygon type')
        if n>6 or (ptype==0 and n<3) or (ptype==1 and n!=2): raise ValueError("invalid polygon vertex count")
        indices=list(p[1:1+n]); neis=list(p[7:7+n])
        if any(v>=c['vert'] for v in indices): raise ValueError("polygon vertex index out of range")
        polygons.append({'tile':tile_number,'index':i,'type':ptype,'area':area_type&63,
                         'flags':p[13],'vertices':[quake(verts[v]) for v in indices],
                         'neighbors_raw':neis})
    off_start=sum(sizes[:-1]); off=[]
    for i in range(c['offmesh']):
        p=struct.unpack_from('<7fHBBI',data,off_start+i*36)
        if not all(math.isfinite(x) for x in p[:7]) or p[6]<0: raise ValueError('invalid offmesh coordinates or radius')
        if not c['offmesh_base']<=p[7]<c['poly'] or polygons[p[7]]['type']!=1:
            raise ValueError("offmesh polygon index/type mismatch")
        off.append({'tile':tile_number,'index':i,'start':quake(p[:3]),'end':quake(p[3:6]),
                    'radius_quake':p[6]*METERS_TO_QUAKE,'polygon':p[7],'flags':p[8],
                    'side':p[9],'user_id':p[10]})
    return {'header':{'tile':[h[2],h[3],h[4]],'user_id':h[5],**c,
                      'walkable_height_quake':h[15]*METERS_TO_QUAKE,
                      'walkable_radius_quake':h[16]*METERS_TO_QUAKE,
                      'walkable_climb_quake':h[17]*METERS_TO_QUAKE,
                      'bounds_quake':[quake(h[18:21]),quake(h[21:24])]},
            'polygons':polygons,'offmesh_connections':off}


def parse_navmesh(data):
    if len(data)<4: raise ValueError('truncated navmesh')
    magic=struct.unpack_from('<I',data)[0]; tiles=[]
    if magic==MSET:
        if len(data)<40: raise ValueError('truncated MSET header')
        _,version,count,*params=struct.unpack_from('<III5f2i',data)
        if version!=1 or not 0<count<=1_000_000: raise ValueError('unsupported MSET header')
        if not all(math.isfinite(v) for v in params[:5]) or params[3]<=0 or params[4]<=0 or params[5]<count or params[6]<=0:
            raise ValueError('invalid MSET parameters')
        pos=40
        refs=set()
        for i in range(count):
            raw,pos2=_take(data,pos,8,'MSET tile header'); ref,size=struct.unpack('<Ii',raw);pos=pos2
            if ref==0 or ref in refs or size<=0: raise ValueError('invalid or duplicate MSET tile descriptor')
            refs.add(ref)
            raw,pos=_take(data,pos,size,'MSET tile data');tiles.append(parse_tile(raw,i))
        if pos!=len(data): raise ValueError('trailing MSET bytes')
        container='MSET1'
    elif magic==DNAV:
        tiles=[parse_tile(data,0)];container='solo'
    else: raise ValueError('unknown navmesh magic')
    polys=[]; off=[]
    for tile in tiles: polys+=tile['polygons'];off+=tile['offmesh_connections']
    ground=[p for p in polys if p['type']==0]
    internal=[]; boundaries=[]; portal=[]
    for p in ground:
        vs=p['vertices']; ns=p['neighbors_raw']
        for edge,(a,b,n) in enumerate(zip(vs,vs[1:]+vs[:1],ns)):
            rec={'polygon':[p['tile'],p['index']],'edge':edge,'start':a,'end':b}
            if n==0: boundaries.append(rec)
            elif n&0x8000: rec['portal_side']=n&0xff;portal.append(rec)
            else:
                target=n-1
                if target>=len(tiles[p['tile']]['polygons']): raise ValueError('internal neighbor out of range')
                rec['neighbor']=[p['tile'],target];internal.append(rec)
    directed={(tuple(x['polygon']),tuple(x['neighbor'])) for x in internal}
    nonrec=[x for x in internal if (tuple(x['neighbor']),tuple(x['polygon'])) not in directed]
    allv=[v for p in ground for v in p['vertices']]
    bounds=[[min(v[i] for v in allv) for i in range(3)],[max(v[i] for v in allv) for i in range(3)]] if allv else None
    return {'container':container,'tile_count':len(tiles),'polygon_count':len(polys),
            'ground_polygon_count':len(ground),'tiles':[t['header'] for t in tiles],
            'ground_polygons':ground,'internal_edges':internal,'boundary_segments':boundaries,
            'portal_segments':portal,'offmesh_connections':off,'bounds_quake':bounds,
            'internal_adjacency_nonreciprocal':nonrec}


def bsp_entities(data):
    header_size=8+18*8
    if len(data)<header_size or data[:4]!=b'RBSP' or struct.unpack_from('<i',data,4)[0]!=1:
        raise ValueError('expected Raven RBSP version 1')
    for i in range(18):
        lo,ln=struct.unpack_from('<ii',data,8+i*8)
        if lo<header_size or ln<0 or lo+ln>len(data): raise ValueError('invalid BSP lump bounds')
    off,size=struct.unpack_from('<ii',data,8); raw,_=_take(data,off,size,'BSP entity lump')
    tokens=[t for t in re.findall(r'"(?:\\.|[^"\\])*"|[{}]',raw.decode('latin1').rstrip('\0'))]
    entities=[];i=0
    while i<len(tokens):
        if tokens[i]!='{': raise ValueError('malformed BSP entities')
        i+=1;e={}
        while i<len(tokens) and tokens[i]!='}':
            if i+1>=len(tokens) or not tokens[i].startswith('"') or not tokens[i+1].startswith('"'): raise ValueError('malformed BSP entity pair')
            e[tokens[i][1:-1]]=tokens[i+1][1:-1];i+=2
        if i>=len(tokens): raise ValueError('unterminated BSP entity');
        i+=1;entities.append(e)
    wanted=[]
    pattern=re.compile(r'team_CTF_(red|blue)(player|spawn|flag)$')
    for index,e in enumerate(entities):
        if not pattern.fullmatch(e.get('classname','')): continue
        try: origin=[float(x) for x in e['origin'].split()]
        except (KeyError,ValueError): raise ValueError('invalid CTF entity origin')
        if len(origin)!=3 or not all(math.isfinite(x) for x in origin): raise ValueError('invalid CTF entity origin')
        angle=float(e.get('angle',0))
        if not math.isfinite(angle): raise ValueError('invalid CTF entity angle')
        wanted.append({'entity_index':index,'classname':e['classname'],'origin':origin,'angle':angle})
    return wanted


def prepare(pk3,bsp_member,navmesh):
    pk3=Path(pk3);navmesh=Path(navmesh)
    with zipfile.ZipFile(pk3) as z:
        names=z.namelist()
        if names.count(bsp_member)!=1: raise ValueError('exact BSP member absent or duplicated in PK3')
        bsp=z.read(bsp_member)
    nav=navmesh.read_bytes(); graph=parse_navmesh(nav); entities=bsp_entities(bsp)
    near=[]
    if graph['bounds_quake']:
        lo,hi=graph['bounds_quake']
        for e in entities:
            o=e['origin'];inside=all(lo[i]-256<=o[i]<=hi[i]+256 for i in range(3))
            near.append({'entity_index':e['entity_index'],'within_nav_bounds_plus_256':inside})
    return {'schema':'strafe-known-map-support-v1','inputs':{'pk3':str(pk3.resolve()),'pk3_sha256':sha(pk3.read_bytes()),
            'bsp_member':bsp_member,'bsp_sha256':sha(bsp),'navmesh':str(navmesh.resolve()),'navmesh_sha256':sha(nav)},
            'support':graph,'ctf_entities':entities,'entity_nav_bounds_diagnostic':near,
            'proposals':[], 'proposal_status':'support inventory only; boundary separation is not evidence of a traversable gap',
            'validation_status':'UNVALIDATED_NATIVE_COLLISION',
            'limitations':['Detour support is rasterized and is not BSP collision geometry.','No agent erosion is inferred beyond values stored in each tile header.','Boundaries may be walls, ledges, tile seams, patches, hazards, or mover interfaces.','Native hull traces and Pmove must validate every future connector and landing.']}


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--pk3',type=Path,required=True);ap.add_argument('--bsp-member',required=True)
    ap.add_argument('--navmesh',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    if a.out.exists(): raise ValueError('output already exists')
    result=prepare(a.pk3,a.bsp_member,a.navmesh);result['tool_sha256']=sha(Path(__file__).read_bytes())
    a.out.mkdir(parents=True);(a.out/'support.json').write_text(json.dumps(result,indent=2,sort_keys=True),newline='\n')
    print(json.dumps({'out':str(a.out.resolve()),'tiles':result['support']['tile_count'],'ground_polygons':result['support']['ground_polygon_count'],'boundaries':len(result['support']['boundary_segments'])}))

if __name__=='__main__': main()
