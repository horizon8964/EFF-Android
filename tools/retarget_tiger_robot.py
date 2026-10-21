"""Bake one Tiger EFMI pose and retarget it to an explicit fixable-robot EFFSOURCE.

Visual model replacement only. The original compute animation is not installed
or executed; robot scripts, collision and interaction remain original.
"""
import argparse
import hashlib
import io
import json
import struct
import sys
import zipfile
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'third_party/eff/tools'))
sys.path.insert(0,str(ROOT/'third_party/eff_blender'))
from nativepack.author_source import AuthorSource,AuthorCore
from nativepack.mesh import native_mesh,unpack_frames
from nativepack.type_tree import encode,require
from nativepack.delta_apply import GEOMETRY_FIELDS
from nativepack.ini_package import export_directory
from eiem_format import read_mesh


def rotate(q,v):
    return v+2*np.cross(q[:,:3],np.cross(q[:,:3],v)+q[:,3:4]*v)


def bake(raw,skin_raw,pose_raw,frame):
    verts=np.frombuffer(raw,dtype='<f4').reshape(-1,14)
    skin=np.frombuffer(skin_raw,dtype=np.dtype([('w','<f4',(4,)),('i','<i4',(4,))]))
    poses=np.frombuffer(pose_raw,dtype='<f4').reshape(-1,270,14)
    require(len(verts)==len(skin) and 0<=frame<len(poses),'frame/vertex layout differs')
    weights=skin['w'].astype(float);indices=skin['i']
    require(np.isfinite(weights).all() and np.all(weights>=0) and np.allclose(weights.sum(1),1,atol=1e-5),'invalid EFMI skin weights')
    require(indices.min()>=0 and indices.max()<270,'pose bone index outside palette')
    channels=poses[frame,indices,:].astype(float)
    require(np.isfinite(channels).all(),'nonfinite pose')
    qr=channels[:,:,6:10];qd=channels[:,:,10:14]
    signs=np.sign(np.sum(qr[:,0:1,:]*qr,axis=2));signs[:,0]=1
    real=np.sum(qr*(weights*signs)[:,:,None],axis=1)
    dual=np.sum(qd*(weights*signs)[:,:,None],axis=1)
    lengths=np.linalg.norm(real,axis=1,keepdims=True)
    require(np.all(lengths>1e-6),'degenerate pose quaternion')
    real/=lengths;dual/=lengths
    scales=np.sum(channels[:,:,:3]*weights[:,:,None],axis=1)
    bias=np.sum(channels[:,:,3:6]*weights[:,:,None],axis=1)
    pos=verts[:,:3].astype(float)[:,[0,2,1]];pos[:,1]*=-1
    pos=pos*scales+bias
    translation=2*(real[:,3:4]*dual[:,:3]-dual[:,3:4]*real[:,:3]+np.cross(real[:,:3],dual[:,:3]))
    pos=rotate(real,pos)+translation
    normals,tangents=unpack_frames(np.frombuffer(raw,dtype='<u4').reshape(-1,14)[:,3])
    def frame_vector(v):
        converted=v[:,[0,2,1]].copy();converted[:,1]*=-1
        converted=rotate(real,converted)
        output=converted[:,[0,2,1]].copy();output[:,2]*=-1
        return output
    normals=frame_vector(normals);tangents[:,:3]=frame_vector(tangents[:,:3])
    pos=pos[:,[0,2,1]].copy();pos[:,2]*=-1
    require(np.isfinite(pos).all() and np.isfinite(normals).all(),'nonfinite baked geometry')
    return verts,pos,normals,tangents


def decode_dds(data):
    raw=bytearray(data)
    if raw[84:88]==b'DX10':
        fmt=struct.unpack_from('<I',raw,128)[0]
        struct.pack_into('<I',raw,128,{99:98,78:77,72:71}.get(fmt,fmt))
    return Image.open(io.BytesIO(raw)).convert('RGBA')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--frame',type=int,default=5)
    p.add_argument('--report',type=Path,default=ROOT/'reports/tiger-robot-conversion.json')
    args=p.parse_args()
    source=AuthorSource(args.baseline,AuthorCore(ROOT/'third_party/eff/nativepack/eff_author_core.dll'))
    require(source.manifest['prefab'].endswith('/int_fixable_robot_postmodel.prefab'),'wrong target prefab')
    resources={r['identity']:r for r in source.manifest['resources']}
    meshes={r['name']:r for r in resources.values() if r['type']=='Mesh'}
    target=meshes['S_interactive_zmdmachine_1_001_s01_lod0']
    original=read_mesh(args.baseline/target['authorFile'])
    original_positions=np.array(original['vertices']).reshape(-1,3)
    low,high=original_positions.min(0),original_positions.max(0)
    use=next(u for u in source.manifest['rendererUses'] if u['mesh']['identity']==target['identity'])
    material=resources[use['materials'][0]['pointer']['identity']]
    slots={x['property'] for x in material['details']['textures']}
    require({'_BaseColorMap','_NormalMap','_MROMap','_EmissiveMap'}<=slots,'unexpected robot shader slot layout')
    parts,materials,textures=[],[],[]
    with zipfile.ZipFile(args.archive) as archive:
        roots=[n[:-len('mod.ini')] for n in archive.namelist() if n.endswith('/mod.ini')]
        require(len(roots)==1,'archive must have one author root')
        prefix=roots[0]
        read=lambda n:archive.read(prefix+n)
        raw=read('Meshes/Component1_VB0.buf')
        verts,positions,normals,tangents=bake(raw,read('Meshes/Component1_PoseBlend.buf'),read('Meshes/pose.buf'),args.frame)
        indices=np.frombuffer(read('Meshes/Component1_IB.buf'),dtype='<u4')
        require(len(indices)==75510+312393 and indices.max()<len(verts),'Tiger draw ranges differ')
        old_low,old_high=positions.min(0),positions.max(0)
        scale=(high[1]-low[1])/(old_high[1]-old_low[1])
        require(np.isfinite(scale) and 0.05<scale<20,'invalid fit scale')
        positions*=scale
        offset=np.array([(low[0]+high[0])/2-(positions[:,0].min()+positions[:,0].max())/2,
                         low[1]-positions[:,1].min(),
                         (low[2]+high[2])/2-(positions[:,2].min()+positions[:,2].max())/2])
        positions+=offset
        report=dict(target_prefab=source.manifest['prefab'],original_target='retargeted to fixable robot; not the original EFMI draw target',
                    archive_sha256=hashlib.sha256(args.archive.read_bytes()).hexdigest(),baked_frame=args.frame,
                    animation_playback=False,scale=float(scale),offset=offset.tolist(),
                    original_bounds=[low.tolist(),high.tolist()],tiger_bounds=[positions.min(0).tolist(),positions.max(0).tolist()],parts=[])
        for group,(first,count,suffix) in enumerate([(0,75510,''),(75510,312393,'2')]):
            mid='MaterialTiger'+str(group);edits=[]
            for role,prop,linear in [('diffuse','_BaseColorMap',False),('normal','_NormalMap',True),('material','_MROMap',True),('light','_EmissiveMap',False)]:
                im=decode_dds(read('Textures/'+role+suffix+'.dds'))
                w,h=im.size;mip=im;pixels=bytearray();mips=0
                while True:
                    pixels.extend(mip.tobytes());mips+=1
                    if mip.size==(1,1):break
                    mip=mip.resize((max(1,mip.width//2),max(1,mip.height//2)),Image.Resampling.BOX)
                tid='TextureTiger'+str(group)+role
                textures.append(dict(id=tid,section=tid,sourceAsset=tid,pixelLabel=tid,pixels=bytes(pixels),
                                     width=w,height=h,format='RGBA32',mipCount=mips,colorSpace=0 if linear else 1,
                                     filter=1,wrap=0,aniso=1,mipBias=0.))
                edits.append(dict(property=prop,texture=tid))
            materials.append(dict(id=mid,nativeTypeHash=material['native']['typeHash'],fieldData={},
                                  sourcePath=material['logicalPath'],sourceAsset=material['name'],textureEdits=edits))
            used,new_indices=np.unique(indices[first:first+count],return_inverse=True)
            n=len(used)
            # One rigid Root binding follows placement without the robot's Head folding animation.
            bone_path='P_interactive_zmdmachine+1_001_s01/Root'
            author=dict(vertex_count=n,vertices=positions[used].ravel().tolist(),normals=normals[used].ravel().tolist(),
                        tangents=tangents[used].ravel().tolist(),colors=[],uvs=[verts[used,4+2*i:6+2*i].ravel().tolist() for i in range(4)]+[[] for _ in range(4)],
                        indices=new_indices.tolist(),skin=[([1.,0.,0.,0.],[0,0,0,0]) for _ in range(n)],
                        bone_paths=[bone_path],bone_hashes=[zlib.crc32(b'Root')],bindposes=[np.eye(4).ravel().tolist()],
                        blend_vertices=[],blend_frames=[],blend_channels=[],blend_weights=[],additional=[])
            name='MeshTiger'+str(group);tree,precision=native_mesh(source.tree(target),author,name)
            parts.append(dict(object=name,partId=name,material=mid,fieldData={f:encode(tree[f]) for f in GEOMETRY_FIELDS},
                              nativeTypeHash=target['native']['typeHash'],vertices=n,indices=len(new_indices),bonePaths=[bone_path],
                              shapeNames=[],shapeControls=[],visibilityBindings=[]))
            report['parts'].append(dict(vertices=n,indices=len(new_indices),precision=precision))
        targets=[]
        for level in range(4):
            r=meshes['S_interactive_zmdmachine_1_001_s01_lod'+str(level)]
            targets.append(dict(level=level,sourcePath=r['logicalPath'],sourceAsset=r['name']))
        rules=[dict(sourcePath=target['logicalPath'],sourceAsset=target['name'],hidden=False,
                    parts=[part['partId'] for part in parts],targets=targets)]
        args.output.mkdir(parents=True,exist_ok=False)
        export_directory(args.output,mod_id='tiger_fixable_robot_static',parts=parts,materials=materials,textures=textures,rules=rules,switches=[])
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False))


if __name__=='__main__':main()
