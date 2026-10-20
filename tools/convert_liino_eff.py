"""Convert Moonlit Violet's default EFMI draw state to EFF format 2.

This is a scoped converter for this package, not a general EFMI interpreter.
Original inputs are read only. Shader injection/menu scripts are not executed.
"""
import argparse
import ast
import copy
import hashlib
import io
import json
import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

WORK = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORK / 'third_party/eff/tools'))
from nativepack.author_source import AuthorSource, AuthorCore
from nativepack.type_tree import encode, require
from nativepack.mesh import native_mesh, unpack_frames
from nativepack.delta_apply import GEOMETRY_FIELDS
from nativepack.ini_package import export_directory


def image(path):
    raw = bytearray(path.read_bytes())
    if raw[:4] == b'DDS ' and raw[84:88] == b'DX10':
        fmt = struct.unpack_from('<I', raw, 128)[0]
        if fmt == 61:
            h, w = struct.unpack_from('<II', raw, 12)
            return Image.frombytes('L', (w, h), bytes(raw[148:148+w*h])).convert('RGBA')
        struct.pack_into('<I', raw, 128, {78: 77, 99: 98, 72: 71}.get(fmt, fmt))
    return Image.open(io.BytesIO(raw)).convert('RGBA')


def sections(text):
    result = {}
    current = None
    for raw in text.splitlines():
        line = raw.split(';', 1)[0].strip()
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1]
            result.setdefault(current, [])
        elif line and current:
            result[current].append(line)
    return result


class DefaultDraws:
    def __init__(self, document):
        self.document, self.values, self.draws = document, {}, []
        for line in document['Constants']:
            m = re.fullmatch(r'global(?: persist)? (\$\w+) = (-?[\d.]+)', line)
            if m:
                self.values[m[1]] = float(m[2])
        self.values['$jet_mode_active'] = 0
        self.values['$lod_level'] = 0
        self.execute('CommandListProcessToggles')

    def evaluate(self, expression):
        expression = re.sub(r'\$\w+', lambda m: str(self.values[m[0]]), expression)
        expression = expression.replace('&&', ' and ').replace('||', ' or ')
        expression = re.sub(r'!(?!=)', ' not ', expression).strip()
        tree = ast.parse(expression, mode='eval')
        allowed = (ast.Expression, ast.Constant, ast.UnaryOp, ast.USub, ast.Not,
                   ast.BoolOp, ast.And, ast.Or, ast.Compare, ast.Eq, ast.NotEq,
                   ast.Lt, ast.LtE, ast.Gt, ast.GtE, ast.Load)
        require(all(isinstance(n, allowed) for n in ast.walk(tree)), 'unsupported EFMI expression')
        return eval(compile(tree, '<numeric EFMI expression>', 'eval'), {'__builtins__': {}}, {})

    def execute(self, name, depth=0):
        require(depth < 8, 'recursive EFMI draw list')
        stack, active = [], True
        for line in self.document[name]:
            if line.startswith('if '):
                take = bool(self.evaluate(line[3:])) if active else False
                stack.append([active, take]); active = active and take
            elif line.startswith('elif '):
                parent, taken = stack[-1]
                take = parent and not taken and bool(self.evaluate(line[5:]))
                stack[-1][1] |= take; active = take
            elif line == 'else':
                parent, taken = stack[-1]; active = parent and not taken; stack[-1][1] = True
            elif line == 'endif':
                active = stack.pop()[0]
            elif active:
                m = re.fullmatch(r'(\$\w+) = (.+)', line)
                if m:
                    self.values[m[1]] = self.evaluate(m[2])
                elif line.startswith('drawindexed = '):
                    self.draws.append((self.ib, tuple(int(v.strip()) for v in line.split('=', 1)[1].split(','))))
                elif line.startswith('ib = ref '):
                    self.ib = line.removeprefix('ib = ref ')
                elif line == 'run = CommandList_Draw_Waist_Skin':
                    self.execute('CommandList_Draw_Waist_Skin', depth+1)
                # Other run statements bind graphics resources, EFMI runtime or HLSL overlays.
        require(not stack, 'unclosed EFMI condition')


def convert(args):
    sys.path.insert(0, str(args.addon))
    sys.path.insert(0, str(WORK / 'third_party/eff_blender'))
    from eiem_format import read_mesh
    text = (args.source/'mod.ini').read_text(encoding='utf-8-sig')
    doc = sections(text)
    author = AuthorSource(args.baseline, AuthorCore(args.addon/'nativepack/eff_author_core.dll'))
    meshes = {r['name']: r for r in author.manifest['resources'] if r['type'] == 'Mesh'}
    resources = {r['identity']: r for r in author.manifest['resources']}
    mapping, palette, source_authors = {}, {}, {}
    for i in range(18):
        sec = '\n'.join(doc[f'TextureOverride_EntryPoint_Component{i}'])
        count = int(re.search(r'match_index_count\s*=\s*(\d+)', sec)[1])
        candidates = []
        for r in meshes.values():
            if not r['name'].endswith('_lod0'):
                continue
            tree = author.tree(r)
            for slot, sub in enumerate(tree['m_SubMeshes'].items()):
                if sub['indexCount'].data == count:
                    candidates.append((r, slot))
        require(len(candidates) == 1, f'component {i}: ambiguous source submesh {count}')
        r, slot = candidates[0]
        mapping[i] = (r, slot)
        source_authors[i] = read_mesh(args.baseline/r['authorFile'])
    offsets = re.findall(r'\$cmp_id = (\d+)\s+\$Pool_MergedSkeleton_Component_VertexGroupOffsets\[\$cmp_id\] = (\d+)\s+\$Pool_MergedSkeleton_Component_VertexGroupCounts\[\$cmp_id\] = (\d+)', text)
    require(len(offsets) == 18, 'merged skeleton table incomplete')
    for cmp, offset, count in offsets:
        a = source_authors[int(cmp)]
        require(int(count) <= len(a['bone_paths']), 'merged palette exceeds source palette')
        for local in range(int(count)):
            index = int(offset)+local
            require(index not in palette, 'overlapping merged bone ranges')
            palette[index] = (a['bone_paths'][local], a['bone_hashes'][local], a['bindposes'][local])
    require(len(palette) == 696, 'merged bone table does not cover all 696 slots')

    # Reviewed identities: exact decoded pixels or < 0.3 thumbnail MSE versus
    # the exported source image, plus the EFMI recorded component set/format.
    texture_names = {
        '151c9982': 'T_actor_common_hairline_03_M', '1ec42d70': 'T_actor_liino_hair_01_P',
        '2dbac2ba': 'T_actor_liino_hair_01_D', '2ed87183': 'T_actor_liino_hair_01_HN',
        '1d6a8e8f': 'T_actor_liino_cloth_01_P', '0bc763f3': 'T_actor_liino_cloth_01_D',
        'a5d661d7': 'T_actor_liino_cloth_01_N', '2946d56b': 'T_actor_liino_cloth_01_M',
        '294012ae': 'T_fx_flow_161_M', '177da9f2': 'T_fx_flow_03_M',
        '290f53c8': 'T_actor_common_face_01_hl_M',
        '2a79e0a2': 'T_actor_common_female_emotion_atlas_01_D',
        'dbf4f80f': 'T_actor_common_female_face_03_cm_M',
    }
    replacement_files = {}
    for f in (args.source/'Textures').glob('*.dds'):
        hash_value = re.search(r't=([0-9a-f]+)', f.name)[1]
        if hash_value in texture_names:
            replacement_files[texture_names[hash_value]] = f
    parts, materials, textures, rules = [], [], [], []
    texture_cache = {}
    report = {'source': str(args.source), 'default_values': {}, 'components': [],
              'limitations': ['Default outfit is baked; EFMI menu and toggle keys are not ported.',
                              'Custom HLSL animated additive overlay is not ported.',
                              'Global graphics textures 9f20bd71, 7d330dbd, bd0ceafb are retained from the game.',
                              'No source shape-key deltas are present in this EFMI package.']}

    pixel_cache = {}
    def texture(f, linear, masked=False, slot_identity=''):
        pixel_key = (str(f),masked)
        key = (str(f), linear, masked, slot_identity)
        if key in texture_cache:
            return texture_cache[key]
        if pixel_key in pixel_cache:
            w,h,pixels,mips = pixel_cache[pixel_key]
            tid = 'Texture'+str(len(textures))
            label = 'Pixels'+hashlib.sha256(repr(pixel_key).encode()).hexdigest()[:16]
            textures.append(dict(id=tid,section=tid,sourceAsset=tid,pixelLabel=label,
                                 pixels=pixels,width=w,height=h,format='RGBA32',mipCount=mips,
                                 colorSpace=0 if linear else 1,filter=1,wrap=0,aniso=1,mipBias=0.))
            texture_cache[key] = tid
            return tid
        im = image(f)
        if masked:
            mask = image(args.source/'dds/tf-alpha-r8.dds').getchannel('R').resize(im.size, Image.Resampling.NEAREST)
            alpha = np.minimum(np.array(im.getchannel('A')), np.where(np.array(mask) < 128, 0, 255).astype('uint8'))
            im.putalpha(Image.fromarray(alpha))
        w, h = im.size
        pixels, mips, mip = bytearray(), 0, im
        while True:
            pixels.extend(mip.tobytes()); mips += 1
            if mip.size == (1, 1):
                break
            mip = mip.resize((max(1, mip.width//2), max(1, mip.height//2)), Image.Resampling.BOX)
        tid = 'Texture'+str(len(textures))
        pixels = bytes(pixels)
        pixel_cache[pixel_key] = (w,h,pixels,mips)
        label = 'Pixels'+hashlib.sha256(repr(pixel_key).encode()).hexdigest()[:16]
        textures.append(dict(id=tid, section=tid, sourceAsset=tid, pixelLabel=label,
                             pixels=bytes(pixels), width=w, height=h, format='RGBA32', mipCount=mips,
                             colorSpace=0 if linear else 1, filter=1, wrap=0, aniso=1, mipBias=0.))
        texture_cache[key] = tid
        return tid

    grouped = {}
    draw_state = DefaultDraws(doc)
    report['default_values'] = {k:v for k,v in draw_state.values.items() if k.startswith('$swapvar')}
    for i in range(18):
        r, slot = mapping[i]
        target = grouped.setdefault(r['name'], dict(sourcePath=r['logicalPath'], sourceAsset=r['name'], hidden=False, parts=[], targets=[]))
        draw_state.draws = []
        draw_state.execute(f'CommandList_Draw_Component{i}')
        draws = list(draw_state.draws)
        row = dict(component=i, mesh=r['name'], submesh=slot, draws=draws)
        report['components'].append(row)
        if not draws:
            row['hidden'] = True
            continue
        # Waist skin is drawn using component 3's buffers but component 7's material.
        by_buffer = {}
        for ib, draw in draws:
            src = int(re.fullmatch(r'Resource_Component(\d+)_IB', ib)[1])
            by_buffer.setdefault(src, []).append(draw)
        use = next(u for u in author.manifest['rendererUses'] if u['mesh']['identity'] == r['identity'])
        mat = resources[use['materials'][slot]['pointer']['identity']]
        mt = author.tree(mat)
        mid = f'MaterialC{i}'
        edits = []
        custom = {}
        if i == 0:
            custom['_BaseMap'] = args.source/'dds/hair.dds'
        if i in (1,3,7,12,14,15):
            custom.update(_BaseMap=args.source/'dds/base.dds', _BumpMap=args.source/'dds/N.dds')
            if i != 7:
                custom['_MetallicGlossMap'] = args.source/'dds/L.dds'
        masked = i in (1,3,7,12,14,15)
        for bind in mat['details']['textures']:
            tex = resources.get(bind['pointer']['identity'])
            f = custom.get(bind['property'])
            if f is None and tex:
                f = replacement_files.get(tex['name'])
            if f:
                linear = author.tree(tex)['m_ColorSpace'].data == 0 if tex else bind['property'] != '_BaseMap'
                edits.append(dict(property=bind['property'], texture=texture(f, linear, masked and bind['property']=='_BaseMap', tex['identity'] if tex else mat['identity']+bind['property'])))
        fields = {}
        if masked:
            for item in mt['m_SavedProperties']['m_Floats'].items():
                if item['first'].data in ('_AlphaClip', '_EnableAlphaTest'):
                    item.set('second', 1.)
            fields['m_SavedProperties'] = encode(mt['m_SavedProperties'])
            if 'm_ValidKeywords' in mt.data:
                words = [v.data for v in mt['m_ValidKeywords'].items()]
                if '_ALPHATEST_ON' not in words:
                    words.append('_ALPHATEST_ON')
                mt['m_ValidKeywords'].set_items(words)
                fields['m_ValidKeywords'] = encode(mt['m_ValidKeywords'])
        materials.append(dict(id=mid, nativeTypeHash=mat['native']['typeHash'], fieldData=fields,
                              sourcePath=mat['logicalPath'], sourceAsset=mat['name'], textureEdits=edits))
        for b, draws_for_buffer in by_buffer.items():
            def raw(kind):
                return (args.source/f'Meshes/Component{b}_{kind}.buf').read_bytes()
            vb0, vb1, vb2 = raw('VB0'), raw('VB1'), raw('VB2')
            nv = len(vb0)//16
            require(len(vb0) == nv*16 and len(vb2) in (nv*16,nv*8), 'vertex stream sizes differ')
            sec = '\n'.join(doc[f'Resource_Component{b}_IB'])
            index_dtype = '<u4' if 'R32_UINT' in sec else '<u2'
            original_indices = np.frombuffer(raw('IB'), dtype=index_dtype)
            selected = []
            for count, first, base in draws_for_buffer:
                require(first+count <= len(original_indices) and count%3 == 0, 'draw exceeds index buffer')
                selected.extend((original_indices[first:first+count].astype(np.int64)+base).tolist())
            unique, new_indices = np.unique(selected, return_inverse=True)
            require(len(unique) and unique.min() >= 0 and unique.max() < nv, 'draw index outside vertex buffer')
            v0 = np.frombuffer(vb0, dtype='u1').reshape(nv,16)[unique].copy()
            stride = len(vb1)//nv
            require(stride in (8,12,16) and len(vb1) == nv*stride, 'unsupported UV stream size')
            v1 = np.frombuffer(vb1, dtype='u1').reshape(nv,stride)[unique].copy()
            if len(vb2) == nv*8:
                skin = np.zeros((len(unique),8),dtype='<u2')
                skin[:,0] = 65535
                skin[:,4:] = np.frombuffer(vb2,dtype='<u2').reshape(nv,4)[unique]
            else:
                skin = np.frombuffer(vb2, dtype='<u2').reshape(nv,8)[unique].copy()
            weights = skin[:,:4].astype(float)/65535
            require(np.all(skin[:,:4].sum(1) == 65535), 'non-unit EFMI weights')
            bone_ids = skin[:,4:].copy()
            used_ids = sorted(set(bone_ids[weights>0].tolist()))
            bones, bone_lookup = [], {}
            for old in used_ids:
                require(old in palette, f'unknown merged bone {old}')
                path, bhash, bindpose = palette[old]
                if path not in bone_lookup:
                    bone_lookup[path] = len(bones); bones.append((path,bhash,bindpose))
                else:
                    require(np.allclose(bones[bone_lookup[path]][2],bindpose,atol=1e-5), 'same bone has incompatible bind pose')
            require(len(bones) <= 256, 'EFF UInt8 bone palette exceeded')
            new_bones = np.zeros_like(bone_ids)
            for old in used_ids:
                new_bones[bone_ids == old] = bone_lookup[palette[old][0]]
            positions = v0.copy().view('<f4').reshape(-1,4)[:,:3].copy()
            normals, tangents = unpack_frames(v0.copy().view('<u4').reshape(-1,4)[:,3])
            a = dict(vertex_count=len(unique), vertices=positions.ravel().tolist(), normals=normals.ravel().tolist(),
                     tangents=tangents.ravel().tolist(), colors=[], uvs=[[] for _ in range(8)],
                     additional=[], indices=new_indices.tolist(), skin=[(w.tolist(),ind.tolist()) for w,ind in zip(weights,new_bones)],
                     bone_paths=[v[0] for v in bones], bone_hashes=[v[1] for v in bones], bindposes=[v[2] for v in bones],
                     blend_vertices=[], blend_frames=[], blend_channels=[], blend_weights=[])
            tree, precision = native_mesh(author.tree(r), a, f'MeshC{i}B{b}')
            channels = [{k:v.data for k,v in c.data.items()} for c in author.tree(mapping[b][0])['m_VertexData']['m_Channels'].items()]
            widths = (4,2,1,1,2,2,1,1,2,2,4,4)
            actual_stride = max(c['offset']+(c['dimension']&15)*widths[c['format']] for c in channels if c['stream']==1 and c['dimension']&15)
            require(stride == actual_stride, 'EFMI UV declaration differs from exact source')
            channels[12] = dict(stream=2, offset=0, format=4, dimension=4)
            channels[13] = dict(stream=2, offset=8, format=6, dimension=4)
            stream2 = np.empty(len(unique),dtype=[('w','<u2',(4,)),('b','u1',(4,))])
            stream2['w'],stream2['b'] = skin[:,:4],new_bones
            payload = bytearray()
            for stream in (v0.tobytes(),v1.tobytes(),stream2.tobytes()):
                payload.extend(stream);payload.extend(bytes((-len(payload))%16))
            tree['m_VertexData']['m_Channels'].set_items(channels)
            tree['m_VertexData'].set('m_DataSize',bytes(payload))
            pid = f'MeshC{i}B{b}'
            parts.append(dict(object=pid, partId=pid, material=mid, fieldData={f:encode(tree[f]) for f in GEOMETRY_FIELDS},
                              nativeTypeHash=r['native']['typeHash'], vertices=len(unique),indices=len(new_indices),
                              bonePaths=a['bone_paths'],shapeNames=[],shapeControls=[],visibilityBindings=[]))
            target['parts'].append(pid)
            row.setdefault('geometry',[]).append(dict(buffer=b,vertices=len(unique),indices=len(new_indices),bones=len(bones),precision=precision))
        print('Converted',i,r['name'],flush=True)
    for rname,rule in grouped.items():
        rule['hidden'] = not rule['parts']
        for level in range(4):
            name = rname[:-1]+str(level)
            if name in meshes:
                rr = meshes[name]
                rule['targets'].append(dict(level=level,sourcePath=rr['logicalPath'],sourceAsset=name))
        rules.append(rule)
    args.output.mkdir(parents=True,exist_ok=False)
    package = export_directory(args.output,mod_id='liino_moonlit_violet_default',parts=parts,materials=materials,
                               textures=textures,rules=rules,switches=[])
    report['result'] = dict(parts=len(parts),materials=len(materials),textures=len(textures),rules=len(rules))
    args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('EFF format 2 verified',report['result'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--addon',type=Path,default=WORK/'third_party/eff',
                        help='Directory containing nativepack/eff_author_core.dll; build with tools/build_author_tools.cmd')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--report',type=Path,default=WORK/'reports/liino-eff-conversion.json')
    convert(parser.parse_args())
