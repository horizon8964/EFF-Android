"""Port this EFMI package's existing 40 vertex frames to EFF shape animation.

Reads original files only. Requires exact desktop bone palettes and an Android
prefab baseline; original INI commands and shaders are never executed.
"""
import argparse,copy,hashlib,json,re,struct,sys,zlib
from pathlib import Path
import numpy as np
from PIL import Image
from convert_liino_eff import sections,image,AuthorSource,AuthorCore

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'third_party/eff_blender'))
from eiem_format import read_mesh
from nativepack.type_tree import encode,require
from nativepack.mesh import native_mesh,unpack_frames,expand_shaped_frames,aabb,vector,transformed_envelope
from nativepack.delta_apply import GEOMETRY_FIELDS
from nativepack.ini_package import export_directory


def draw_groups(lines):
    groups={None:[],0:[],1:[]};state=None
    for line in lines:
        if line.startswith('if $saber == '):state=int(line.rsplit(' ',1)[1])
        elif line=='endif':state=None
        elif line.startswith('drawindexed = '):
            groups[state].append(tuple(map(int,line.split('=',1)[1].split(','))))
    return {k:v for k,v in groups.items() if v}


def shape_bytes(template,frames,base_n,base_t):
    """Pack fixed-layout shape records in NumPy, avoiding millions of tree nodes."""
    shape=copy.deepcopy(template['m_Shapes'])
    element=shape['vertices'].array_node().schema.children[1]
    require([c.name for c in element.children]==['vertex','normal','tangent','index'],'shape record layout')
    require(all([v.name for v in c.children]==['x','y','z'] and all(v.type=='float' for v in c.children)
                for c in element.children[:3]) and element.children[3].type in ('UInt32','unsigned int'),'shape record scalar layout: '+repr(element))
    require(not any(c.align for c in element.children) and not element.align,'shape record alignment')
    records=[];descriptors=[];weights=[];start=0
    base=frames[0][:,:3];low=base.copy();high=base.copy()
    for f,frame in enumerate(frames[1:],1):
        n,t=unpack_frames(frame[:,3].copy().view('<u4'))
        delta=frame[:,:3]-base;dn=n-base_n;dt=t[:,:3]-base_t[:,:3]
        used=np.flatnonzero(np.any(delta!=0,axis=1)|np.any(dn!=0,axis=1)|np.any(dt!=0,axis=1))
        block=np.empty(len(used),dtype=[('p','<f4',(3,)),('n','<f4',(3,)),('t','<f4',(3,)),('id','<u4')])
        block['p'],block['n'],block['t'],block['id']=delta[used],dn[used],dt[used],used
        records.append(block.tobytes());descriptors.append(dict(firstVertex=start,vertexCount=len(used),hasNormals=True,hasTangents=True))
        start+=len(used);weights.append(f/40*100)
        low=np.minimum(low,frame[:,:3]);high=np.maximum(high,frame[:,:3])
    # A final copy of frame zero permits smooth wraparound from frame 39.
    descriptors.append(dict(firstVertex=start,vertexCount=0,hasNormals=True,hasTangents=True));weights.append(100.)
    shape['shapes'].set_items(descriptors)
    shape['channels'].set_items([dict(name='EFMI_Loop',nameHash=zlib.crc32(b'EFMI_Loop'),frameIndex=0,frameCount=40)])
    shape['fullWeights'].set_items(weights)
    vertices=struct.pack('<i',start)+b''.join(records)
    if shape['vertices'].array_node().schema.align or shape['vertices'].schema.align:
        vertices+=bytes((-len(vertices))%4)
    return vertices+encode(shape['shapes'])+encode(shape['channels'])+encode(shape['fullWeights']),low,high


def convert(args):
    core=AuthorCore(ROOT/'third_party/eff/nativepack/eff_author_core.dll')
    pc=AuthorSource(args.pc,core);mobile=AuthorSource(args.mobile,core)
    doc=sections((args.source/'mod.ini').read_text(encoding='utf-8-sig'))
    require('global $frameEnd = 39' in doc['Constants'] and 'global $fps = 60' in doc['Constants'],'unexpected animation timing')
    pc_mesh={r['name']:r for r in pc.manifest['resources'] if r['type']=='Mesh'}
    mob_mesh={r['name']:r for r in mobile.manifest['resources'] if r['type']=='Mesh'}
    resources={r['identity']:r for r in mobile.manifest['resources']}
    mapping={};pal={};originals={}
    for i in range(11):
        count=int(re.search(r'match_index_count\s*=\s*(\d+)','\n'.join(doc[f'TextureOverride_EntryPoint_Component{i}']))[1])
        matches=[]
        for r in pc_mesh.values():
            if not r['name'].endswith('_lod0'):continue
            for slot,s in enumerate(pc.tree(r)['m_SubMeshes'].items()):
                if s['indexCount'].data==count:matches.append((r,slot))
        require(len(matches)==1,f'ambiguous component {i}')
        pr,slot=matches[0];mr=mob_mesh[pr['name'][:-1]+'1'];mapping[i]=(mr,slot)
        originals[i]=read_mesh(args.pc/pr['authorFile'])
    text=(args.source/'mod.ini').read_text(encoding='utf-8-sig')
    for cmp,offset,count in re.findall(r'\$cmp_id = (\d+)\s+\$Pool_MergedSkeleton_Component_VertexGroupOffsets\[\$cmp_id\] = (\d+)\s+\$Pool_MergedSkeleton_Component_VertexGroupCounts\[\$cmp_id\] = (\d+)',text):
        original=originals[int(cmp)]
        require(int(count)<=len(original['bone_paths']),'source palette mismatch')
        for k in range(int(count)):
            require(int(offset)+k not in pal,'overlapping palettes')
            pal[int(offset)+k]=(original['bone_paths'][k],original['bone_hashes'][k],original['bindposes'][k])
    require(len(pal)==395,'incomplete merged skeleton')
    parts=[];materials=[];textures=[];rules=[];report={'frames':40,'fps':60,'duration':40/60,'parts':[]}
    texfiles={re.search(r't=([0-9a-f]+)',p.name)[1]:p for p in (args.source/'Textures').glob('*.dds')}
    texture_cache={}
    pixel_cache={}
    def texture(path,linear,slot):
        if (path,linear,slot) in texture_cache:return texture_cache[path,linear,slot]
        im=image(path)
        if slot.endswith('_BumpMap'):
            raw=path.read_bytes()
            require(raw[:4]==b'DDS ' and raw[84:88]==b'DX10' and struct.unpack_from('<I',raw,128)[0] in (83,98),'normal conversion requires reviewed two-channel UNorm data')
            pixels=np.array(im)
            require(pixels[:,:,2].max()<=4,'normal already contains Z; do not reconstruct twice')
            xy=pixels[:,:,:2].astype('f4')*(2./255)-1
            pixels[:,:,2]=np.rint((np.sqrt(np.maximum(0,1-np.sum(xy*xy,axis=2)))+1)*127.5).astype('u1')
            pixels[:,:,3]=255
            im=Image.fromarray(pixels)
        w,h=im.size;data=bytearray();mips=0
        while True:
            data.extend(im.tobytes());mips+=1
            if im.size==(1,1):break
            im=im.resize((max(1,im.width//2),max(1,im.height//2)),Image.Resampling.BOX)
        tid='Texture'+str(len(textures))
        pixels=pixel_cache.setdefault(path,bytes(data))
        textures.append(dict(id=tid,section=tid,sourceAsset=tid,pixelLabel='Pixels'+hashlib.sha256(str(path).encode()).hexdigest()[:16],pixels=pixels,width=w,height=h,
                             format='RGBA32',mipCount=mips,colorSpace=0 if linear else 1,filter=1,wrap=0,aniso=1,mipBias=0.))
        texture_cache[path,linear,slot]=tid
        return tid
    for i,(r,slot) in mapping.items():
        targets=[dict(level=level-1,sourcePath=mob_mesh[r['name'][:-1]+str(level)]['logicalPath'],sourceAsset=r['name'][:-1]+str(level))
                 for level in range(1,4) if r['name'][:-1]+str(level) in mob_mesh]
        rule=dict(sourcePath=r['logicalPath'],sourceAsset=r['name'],hidden=i not in (5,9),parts=[],targets=targets)
        rules.append(rule)
        if rule['hidden']:continue
        use=next(u for u in mobile.manifest['rendererUses'] if u['mesh']['identity']==r['identity'])
        mat=resources[use['materials'][slot]['pointer']['identity']];mid=f'MaterialC{i}';edits=[]
        if i in (5,9):
            binds=[('_BaseMap','c624cf92'),('_BumpMap','b2b16f01')]
            if i==9:binds.append(('_MetallicGlossMap','9d38d199'))
            for prop,h in binds:
                require(prop in {x['property'] for x in mat['details']['textures']},'missing material slot')
                edits.append(dict(property=prop,texture=texture(texfiles[h],prop!='_BaseMap',mat['identity']+prop)))
        materials.append(dict(id=mid,nativeTypeHash=mat['native']['typeHash'],fieldData={},sourcePath=mat['logicalPath'],sourceAsset=mat['name'],textureEdits=edits))
        prefix=args.source/'Meshes';raw=np.fromfile(prefix/f'Component{i}_VB0.buf',dtype='<f4').reshape(-1,4);nv=len(raw)
        frames=[np.fromfile(prefix/f'Component{i}_VB/Component{i}_VB{f:02}.buf',dtype='<f4').reshape(-1,4) for f in range(40)]
        require(all(f.shape==raw.shape and np.all(np.isfinite(f[:,:3])) for f in frames),'frame geometry mismatch')
        skin=np.fromfile(prefix/f'Component{i}_VB2.buf',dtype='<u2').reshape(nv,8)
        require(np.all(skin[:,:4].astype('u4').sum(1)==65535),'invalid skin weights')
        uvraw=(prefix/f'Component{i}_VB1.buf').read_bytes();stride=len(uvraw)//nv
        require(stride==12,'unexpected UV stride')
        channels=[{k:v.data for k,v in c.data.items()} for c in pc.tree(pc_mesh[r['name'][:-1]+'0'])['m_VertexData']['m_Channels'].items()]
        widths=(4,2,1,1,2,2,1,1,2,2,4,4)
        require(max(c['offset']+(c['dimension']&15)*widths[c['format']] for c in channels if c['stream']==1 and c['dimension']&15)==stride,'source UV layout mismatch')
        index=np.fromfile(prefix/f'Component{i}_IB.buf',dtype='<u4' if i==5 else '<u2')
        for variant,draws in draw_groups(doc[f'CommandList_Draw_Component{i}']).items():
            selected=np.concatenate([index[first:first+count].astype('i8')+base for count,first,base in draws])
            used,remap=np.unique(selected,return_inverse=True);require(used.min()>=0 and used.max()<nv,'index outside mesh')
            weights=skin[used,:4].astype(float)/65535;ids=skin[used,4:];bones=[];lookup={};newids=np.zeros_like(ids)
            for old in sorted(set(ids[weights>0].tolist())):
                path,bhash,bind=pal[old]
                if path not in lookup:lookup[path]=len(bones);bones.append((path,bhash,bind))
                else:require(np.allclose(bones[lookup[path]][2],bind,atol=1e-5),'incompatible same-name bindpose')
                newids[ids==old]=lookup[path]
            require(len(bones)<=256,'bone palette exceeds byte storage')
            f=[v[used].copy() for v in frames];n,t=unpack_frames(f[0][:,3].copy().view('<u4'));name=f'MeshC{i}V'+('Common' if variant is None else str(variant))
            author=dict(vertex_count=len(used),vertices=f[0][:,:3].ravel().tolist(),normals=n.ravel().tolist(),tangents=t.ravel().tolist(),colors=[],uvs=[[] for _ in range(8)],
                        indices=remap.tolist(),skin=[(w.tolist(),b.tolist()) for w,b in zip(weights,newids)],bone_paths=[x[0] for x in bones],bone_hashes=[x[1] for x in bones],bindposes=[x[2] for x in bones],additional=[],blend_vertices=[],blend_frames=[],blend_channels=[],blend_weights=[])
            tree,precision=native_mesh(mobile.tree(r),author,name)
            c=copy.deepcopy(channels);c[12]=dict(stream=2,offset=0,format=4,dimension=4);c[13]=dict(stream=2,offset=8,format=6,dimension=4)
            s2=np.empty(len(used),dtype=[('w','<u2',(4,)),('b','u1',(4,))]);s2['w']=skin[used,:4];s2['b']=newids
            payload=bytearray()
            for stream in (f[0].tobytes(),np.frombuffer(uvraw,dtype='u1').reshape(nv,stride)[used].tobytes(),s2.tobytes()):
                payload.extend(stream);payload.extend(bytes((-len(payload))%16))
            tree['m_VertexData']['m_Channels'].set_items(c);tree['m_VertexData'].set('m_DataSize',bytes(payload))
            shapes,low,high=shape_bytes(tree,f,n,t)
            # Request the CPU shape layout while keeping shape payload packed.
            tree['m_Shapes']['channels'].set_items([dict(name='EFMI_Loop',nameHash=zlib.crc32(b'EFMI_Loop'),frameIndex=0,frameCount=40)])
            expand_shaped_frames(tree)
            bounds=aabb(np.vstack((low,high)));tree.set('m_LocalAABB',bounds);tree['m_SubMeshes'].items()[0].set('localAABB',bounds)
            perbone=[]
            for b,(_,_,bind) in enumerate(bones):
                mask=np.any((newids==b)&(weights>0),axis=1)
                lo,hi=transformed_envelope(low[mask],high[mask],np.array(bind).reshape(4,4,order='F'))
                perbone.append(dict(m_Min=vector(lo),m_Max=vector(hi)))
            tree['m_BonesAABB'].set_items(perbone)
            fields={k:(shapes if k=='m_Shapes' else encode(tree[k])) for k in GEOMETRY_FIELDS}
            control=dict(enabled=True,identity='Loop',shape='EFMI_Loop',label='动画进度',minimum=0.,maximum=1.,default=0.,hotkey_increase='',hotkey_decrease='',hotkey_speed=1.)
            clip=dict(id='EFMILoop',label='原版动态循环',key='',duration=40/60,speed=1.,loop=True,autoplay=True,channels={'EFMI_Loop':[0.,1.]})
            parts.append(dict(object=name,partId=name,material=mid,fieldData=fields,nativeTypeHash=r['native']['typeHash'],vertices=len(used),indices=len(remap),bonePaths=author['bone_paths'],shapeNames=['EFMI_Loop'],shapeControls=[control],shapeAnimations=[clip],visibilityBindings=[] if variant is None else [dict(variable='$saber',visible=[variant])]))
            rule['parts'].append(name);report['parts'].append(dict(name=name,component=i,variant=variant,vertices=len(used),indices=len(remap),bones=len(bones),shapeBytes=len(shapes),bounds=bounds))
            print('Converted',name,len(used),'vertices',len(shapes),'shape bytes',flush=True)
    args.output.mkdir(parents=True,exist_ok=False)
    export_directory(args.output,mod_id='endminf_x_tephros_dynamic',parts=parts,materials=materials,textures=textures,rules=rules,switches=[dict(variable='$saber',default=1,key='p',stateCount=2)])
    args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Published',args.output,flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--pc',type=Path,required=True);p.add_argument('--mobile',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--report',type=Path,default=ROOT/'reports/endmin-dynamic-conversion.json')
    convert(p.parse_args())
