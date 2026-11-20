"""Standalone GLA/GLM reader + GLM writer (no bpy). Layout follows the JKA Blender add-on."""
import struct, numpy as np

def _s(b): return b.split(b'\0')[0].decode('latin1')

def read_gla(path):
    f = open(path,'rb').read()
    ident,ver,name,scale,nframes,ofsframes,nbones,ofscomp,ofsskel,ofsend = struct.unpack_from('4si64sf6i',f,0)
    assert ident==b'2LGA' and ver==6
    base = 4*2+64+4*7
    offs = struct.unpack_from('%di'%nbones,f,base)
    bones=[]
    for i,o in enumerate(offs):
        p=base+o
        nm=_s(f[p:p+64]); flags,parent=struct.unpack_from('Ii',f,p+64)
        m=np.array(struct.unpack_from('12f',f,p+72)).reshape(3,4)
        mi=np.array(struct.unpack_from('12f',f,p+72+48)).reshape(3,4)
        nch,=struct.unpack_from('i',f,p+72+96)
        ch=list(struct.unpack_from('%di'%nch,f,p+72+96+4))
        bones.append(dict(index=i,name=nm,flags=flags,parent=parent,base=m,baseinv=mi,children=ch))
    return dict(name=_s(name),numFrames=nframes,numBones=nbones,bones=bones)

def read_glm(path):
    f=open(path,'rb').read()
    ident,ver,name,anim,_,nbones,nlod,ofslod,nsurf,ofshier,ofsend=struct.unpack_from('4si64s64s7i',f,0)
    assert ident==b'2LGM' and ver==6
    base=4*2+128+28
    soffs=struct.unpack_from('%di'%nsurf,f,base)
    surfs=[]
    for o in soffs:
        p=base+o
        nm=_s(f[p:p+64]); flags=struct.unpack_from('I',f,p+64)[0]; shader=_s(f[p+68:p+132])
        _,parent,nch=struct.unpack_from('3i',f,p+132)
        ch=list(struct.unpack_from('%di'%nch,f,p+144))
        surfs.append(dict(name=nm,flags=flags,shader=shader,parent=parent,children=ch))
    lods=[]
    p=ofslod
    for l in range(nlod):
        lofsend,=struct.unpack_from('i',f,p)
        so=struct.unpack_from('%di'%nsurf,f,p+4)
        out=[]
        for i,o in enumerate(so):
            sp=p+4+o
            ident_,idx,ofshdr,nv,ofsv,nt,ofst,nbr,ofsbr,sofsend=struct.unpack_from('10i',f,sp)
            verts=[]
            for v in range(nv):
                vp=sp+ofsv+v*32
                nx,ny,nz,x,y,z,packed=struct.unpack_from('6fI',f,vp)
                w=struct.unpack_from('4B',f,vp+28)
                nw=(packed>>30)+1
                ws=[];tot=0
                for k in range(nw):
                    rw=(w[k]|(((packed>>(20+2*k))&3)<<8))/1023
                    if k+1<nw: tot+=rw; ws.append(rw)
                    else: ws.append(1-tot)
                bi=[(packed>>(5*k))&31 for k in range(nw)]
                verts.append(dict(co=(x,y,z),n=(nx,ny,nz),w=ws,b=bi))
            uvp=sp+ofsv+nv*32
            for v in range(nv):
                verts[v]['uv']=struct.unpack_from('2f',f,uvp+8*v)
            tris=[struct.unpack_from('3i',f,sp+ofst+12*t) for t in range(nt)]
            brefs=list(struct.unpack_from('%di'%nbr,f,sp+ofsbr))
            out.append(dict(verts=verts,tris=tris,brefs=brefs,size=sofsend))
        lods.append(out)
        p+=lofsend
    return dict(name=_s(name),anim=_s(anim),numBones=nbones,surfaces=surfs,lods=lods)

def write_glm(path, glm_name, gla_name, num_bones, surfaces):
    """surfaces: list of dict(name, flags(1 tag,2 off), shader, parent(-1 or idx), verts, tris, brefs)
    verts: dict(co,n,uv(u,v already in glm convention),w[list],b[list local bone idx]).
    tris: (a,b,c) in glm file order. Single LOD."""
    nsurf=len(surfaces)
    # hierarchy
    kids={i:[] for i in range(nsurf)}
    for i,s in enumerate(surfaces):
        if s['parent']>=0: kids[s['parent']].append(i)
    hblobs=[]
    for i,s in enumerate(surfaces):
        b=struct.pack('64sI64s3i',s['name'].encode(),s['flags'],s['shader'].encode(),0,s['parent'],len(kids[i]))
        b+=struct.pack('%di'%len(kids[i]),*kids[i])
        hblobs.append(b)
    hoffs=[];o=4*nsurf
    for b in hblobs: hoffs.append(o); o+=len(b)
    hsize=o
    ofsLODs=164+hsize
    # surfaces
    sblobs=[]
    for i,s in enumerate(surfaces):
        nv=len(s['verts']);nt=len(s['tris']);nb=len(s['brefs'])
        assert nb<=32 and all(len(v['w'])<=4 for v in s['verts'])
        ofst=40; ofsv=ofst+12*nt; ofsb=ofsv+40*nv; end=ofsb+4*nb
        # sizeof header = 10 ints
        b=struct.pack('10i',0,i,0,nv,ofsv,nt,ofst,nb,ofsb,end)  # ofsHeader patched later
        b+=b''.join(struct.pack('3i',*t) for t in s['tris'])
        vb=bytearray()
        for v in s['verts']:
            nw=len(v['w']); packed=(nw-1)<<30; ws=[0,0,0,0]
            # integer weights summing to 1023; last weight implied
            iw=[int(round(x*1023)) for x in v['w']]
            iw[-1]=1023-sum(iw[:-1]) if nw>1 else 1023
            for k in range(nw):
                ws[k]=iw[k]&255; packed|=((iw[k]>>8)&3)<<(20+2*k); packed|=(v['b'][k]&31)<<(5*k)
            vb+=struct.pack('6fI4B',*v['n'],*v['co'],packed,*ws)
        for v in s['verts']: vb+=struct.pack('2f',*v['uv'])
        b+=bytes(vb)+struct.pack('%di'%nb,*s['brefs'])
        assert len(b)==end
        sblobs.append(b)
    loffs=[];o=4*nsurf
    for b in sblobs: loffs.append(o); o+=len(b)
    lodsize=4+o
    ofsEnd=ofsLODs+lodsize
    out=bytearray()
    out+=struct.pack('4si64s64s7i',b'2LGM',6,glm_name.encode(),gla_name.encode(),0,num_bones,1,ofsLODs,nsurf,164+4*nsurf,ofsEnd)
    out+=struct.pack('%di'%nsurf,*hoffs)
    for b in hblobs: out+=b
    assert len(out)==ofsLODs
    out+=struct.pack('i',lodsize)+struct.pack('%di'%nsurf,*loffs)
    for b in sblobs:
        start=len(out)
        out+=b
        # patch ofsHeader = -(offset of surface from file start)
        struct.pack_into('i',out,start+8,-start)
    assert len(out)==ofsEnd
    open(path,'wb').write(bytes(out))
    return len(out)
