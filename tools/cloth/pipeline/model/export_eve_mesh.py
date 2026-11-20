import bpy, numpy as np, json, sys
out=sys.argv[sys.argv.index('--')+1]
arm=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]
ob=bpy.data.objects['Eve']; me=ob.data
info={'pose_position':arm.data.pose_position,'has_action':bool(arm.animation_data and arm.animation_data.action),
      'shape_key_values':[(k.name,k.value) for k in (me.shape_keys.key_blocks if me.shape_keys else []) if abs(k.value)>1e-6],
      'mesh_matrix_world':[list(r) for r in ob.matrix_world]}
print('INFO',info)
n=len(me.vertices)
pos=np.empty(n*3); me.vertices.foreach_get('co',pos); pos=pos.reshape(n,3)
me.calc_loop_triangles()
lt=me.loop_triangles
T=len(lt)
tv=np.empty(T*3,dtype=np.int32); lt.foreach_get('vertices',tv); tv=tv.reshape(T,3)
tl=np.empty(T*3,dtype=np.int32); lt.foreach_get('loops',tl); tl=tl.reshape(T,3)
mat=np.empty(T,dtype=np.int32); lt.foreach_get('material_index',mat)
uvs={}
for u in me.uv_layers:
    a=np.empty(len(me.loops)*2); u.data.foreach_get('uv',a); a=a.reshape(-1,2)
    uvs[u.name]=a[tl]  # (T,3,2)
# weights
vg=[g.name for g in ob.vertex_groups]
rows=[];cols=[];vals=[]
for v in me.vertices:
    for g in v.groups:
        rows.append(v.index);cols.append(g.group);vals.append(g.weight)
# normals (custom/split) per loop
nl=np.empty(len(me.loops)*3); me.corner_normals.foreach_get('vector',nl) if hasattr(me,'corner_normals') else None
nl=nl.reshape(-1,3)[tl]
np.savez_compressed(out,pos=pos,tv=tv,mat=mat,uv0=uvs['UV0'],uv1=uvs['UV1'],cn=nl,
    wr=np.array(rows,dtype=np.int32),wc=np.array(cols,dtype=np.int32),wv=np.array(vals,dtype=np.float32))
json.dump(dict(info=info,vgroups=vg,materials=[s.material.name for s in ob.material_slots],
    bones=[dict(name=b.name,parent=b.parent.name if b.parent else None,head=list(b.head_local),tail=list(b.tail_local),
                mat=[list(r) for r in b.matrix_local]) for b in arm.data.bones]),open(out+'.json','w'))
print('done',n,T)
