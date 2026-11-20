import bpy, json, sys
out={}
out['objects']=[]
for o in bpy.data.objects:
    d={'name':o.name,'type':o.type,'parent':o.parent.name if o.parent else None,
       'loc':list(o.location),'rot':list(o.rotation_euler),'scale':list(o.scale),'dims':list(o.dimensions),
       'hide':o.hide_get() if hasattr(o,'hide_get') else None}
    if o.type=='MESH':
        m=o.data
        d['verts']=len(m.vertices); d['polys']=len(m.polygons)
        d['tris']=sum(len(p.vertices)-2 for p in m.polygons)
        d['materials']=[s.material.name if s.material else None for s in o.material_slots]
        d['uv']=[u.name for u in m.uv_layers]
        d['vgroups']=len(o.vertex_groups)
        d['mods']=[(mm.type,mm.name) for mm in o.modifiers]
        d['shapekeys']=len(m.shape_keys.key_blocks) if m.shape_keys else 0
        # per material poly count
        pc={}
        for p in m.polygons: pc[p.material_index]=pc.get(p.material_index,0)+1
        d['poly_by_slot']=pc
    out['objects'].append(d)
arm=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]
out['bones']=[{'name':b.name,'parent':b.parent.name if b.parent else None,'head':list(b.head_local),'tail':list(b.tail_local)} for b in arm.data.bones]
out['arm_matrix_world']=[list(r) for r in arm.matrix_world]
out['materials']=[m.name for m in bpy.data.materials]
out['unit']=bpy.context.scene.unit_settings.scale_length
json.dump(out,open(sys.argv[sys.argv.index('--')+1],'w'),indent=1)
