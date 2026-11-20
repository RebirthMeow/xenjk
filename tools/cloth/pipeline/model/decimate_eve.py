"""Blender batch: split Eve by material, decimate (collapse) each to a tri target, dump per-material arrays.
  blender -b eve_2b.blend --python decimate_eve.py -- targets.json out.npz
targets.json: {material: target_tris or null/0 (keep)}"""
import bpy, bmesh, numpy as np, json, sys
tj, out = sys.argv[sys.argv.index('--')+1:][:2]
targets = json.load(open(tj))
arm=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]
ob=bpy.data.objects['Eve']
bpy.context.view_layer.objects.active=ob
ob.shape_key_clear()
for m in list(ob.modifiers): ob.modifiers.remove(m)
mats=[s.material.name for s in ob.material_slots]
vg=[g.name for g in ob.vertex_groups]
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.separate(type='MATERIAL')
bpy.ops.object.mode_set(mode='OBJECT')
pieces=[o for o in bpy.data.objects if o.type=='MESH']
res={}
meta={}
for o in pieces:
    mi=o.data.polygons[0].material_index if len(o.data.polygons) else -1
    # separate keeps slot list on each piece; find material by the face's slot
    name=o.material_slots[mi].material.name
    t=targets.get(name)
    n=len(o.data.polygons)
    if t and t<n:
        bpy.context.view_layer.objects.active=o
        md=o.modifiers.new('dec','DECIMATE'); md.decimate_type='COLLAPSE'; md.ratio=t/n; md.use_collapse_triangulate=True
        md.use_symmetry=False
        bpy.ops.object.modifier_apply(modifier='dec')
    me=o.data
    me.calc_loop_triangles()
    n=len(me.vertices); lt=me.loop_triangles; T=len(lt)
    pos=np.empty(n*3); me.vertices.foreach_get('co',pos); pos=pos.reshape(n,3)
    tv=np.empty(T*3,dtype=np.int32); lt.foreach_get('vertices',tv); tv=tv.reshape(T,3)
    tl=np.empty(T*3,dtype=np.int32); lt.foreach_get('loops',tl); tl=tl.reshape(T,3)
    uvd=np.empty(len(me.loops)*2); me.uv_layers['UV0'].data.foreach_get('uv',uvd); uv=uvd.reshape(-1,2)[tl]
    names=[g.name for g in o.vertex_groups]
    rows=[];cols=[];vals=[]
    for v in me.vertices:
        for g in v.groups:
            rows.append(v.index);cols.append(vg.index(names[g.group]));vals.append(g.weight)
    res[name+'/pos']=pos; res[name+'/tv']=tv; res[name+'/uv']=uv
    res[name+'/wr']=np.array(rows,dtype=np.int32);res[name+'/wc']=np.array(cols,dtype=np.int32);res[name+'/wv']=np.array(vals,dtype=np.float32)
    meta[name]=dict(verts=n,tris=T)
    print('PIECE',name,n,T,flush=True)
np.savez_compressed(out,**res)
json.dump(dict(vgroups=vg,pieces=meta),open(out+'.json','w'))
