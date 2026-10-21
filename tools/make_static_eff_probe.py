"""Create a small visual-only EFF probe from an explicitly supplied EFFSOURCE.

Requires user-owned source resources. No collision or gameplay state edits.
The result is a test Mod, not a conversion of an unrelated EFMI package.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'third_party/eff/tools'))
sys.path.insert(0,str(ROOT/'third_party/eff_blender'))
from nativepack.author_source import AuthorSource,AuthorCore
from nativepack.mesh import native_mesh
from nativepack.delta_apply import GEOMETRY_FIELDS
from nativepack.type_tree import encode,require
from nativepack.ini_package import export_directory
from eiem_format import read_mesh


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--mesh',required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--core',type=Path,default=ROOT/'third_party/eff/nativepack/eff_author_core.dll')
    p.add_argument('--scale',type=float,default=1.05)
    args=p.parse_args()
    require(0.5<=args.scale<=1.5,'probe scale outside visual test bounds')
    source=AuthorSource(args.baseline,AuthorCore(args.core))
    resources={r['identity']:r for r in source.manifest['resources']}
    matches=[r for r in resources.values() if r['type']=='Mesh' and r['name']==args.mesh]
    require(len(matches)==1,'probe needs one exact source Mesh')
    r=matches[0]
    uses=[u for u in source.manifest['rendererUses'] if u['mesh']['identity']==r['identity']]
    require(uses and all(u['type']=='MeshRenderer' for u in uses),'probe Mesh has a skinned consumer')
    require(r['details']['submeshes']==1 and r['details']['bindposes']==0,'probe needs one unskinned submesh')
    material=resources[uses[0]['materials'][0]['pointer']['identity']]
    author=read_mesh(args.baseline/r['authorFile'])
    require(not author['bone_paths'] and not author['blend_channels'],'probe source unexpectedly has bones/shapes')
    author['vertices']=[v*args.scale for v in author['vertices']]
    tree,_=native_mesh(source.tree(r),author,'MeshInfrastructureProbe')
    part=dict(object='MeshInfrastructureProbe',partId='probe',material='MaterialInfrastructureProbe',
              fieldData={f:encode(tree[f]) for f in GEOMETRY_FIELDS},nativeTypeHash=r['native']['typeHash'],
              vertices=author['vertex_count'],indices=len(author['indices']),bonePaths=[],shapeNames=[],
              shapeControls=[],visibilityBindings=[])
    mat=dict(id='MaterialInfrastructureProbe',nativeTypeHash=material['native']['typeHash'],fieldData={},
             sourcePath=material['logicalPath'],sourceAsset=material['name'],textureEdits=[])
    rule=dict(sourcePath=r['logicalPath'],sourceAsset=r['name'],hidden=False,parts=['probe'],
              targets=[dict(level=0,sourcePath=r['logicalPath'],sourceAsset=r['name'])])
    args.output.mkdir(parents=True,exist_ok=False)
    export_directory(args.output,mod_id='infrastructure_static_probe',parts=[part],materials=[mat],textures=[],rules=[rule],switches=[])
    print(json.dumps(dict(prefab=source.manifest['prefab'],mesh=args.mesh,vertices=author['vertex_count'],
                         indices=len(author['indices']),scale=args.scale),ensure_ascii=False))


if __name__=='__main__':main()
