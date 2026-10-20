"""Package host-compiled Android startup substitutions for local device testing."""
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parent.parent
compiled = root / 'research/si-android/compiled'
output = root / 'research/si-device-pack'
if output.exists():
    raise FileExistsError('refusing to replace an existing device pack')
(output / 'bundles').mkdir(parents=True)
lines = (compiled / 'routes.tsv').read_text(encoding='utf-8').splitlines()
if lines[0] != 'EFF_ANDROID_ROUTES\t1':
    raise ValueError('unexpected route format')
payloads, routes = [], [lines[0]]
for line in lines[1:]:
    columns = line.split('\t')
    source = Path(columns[1])
    columns[1] = 'VFS/' + source.parent.name + '/' + source.name
    name = columns[5]
    original = compiled / 'generation/startup' / name
    data = original.read_bytes()
    if hashlib.sha256(data).hexdigest().upper() != columns[7]:
        raise ValueError('compiled startup payload changed')
    shutil.copy2(original, output / 'bundles' / name)
    payloads.append(dict(file='bundles/' + name, bytes=len(data), sha256=columns[7]))
    routes.append('\t'.join(columns))
(output / 'routes.tsv').write_text('\n'.join(routes) + '\n', encoding='utf-8', newline='\n')
selection = json.loads((root / 'research/si-android/android-source-inputs.json').read_text(encoding='utf-8'))
# The FBX container is loaded by the character lifecycle; it is not a direct
# ResourceManager Object asset on this client. Lease only standalone donors.
donors = sorted(path for path in selection['assets'] if not path.endswith('.fbx'))
(output / 'prewarm.txt').write_text('\n'.join(donors) + '\n', encoding='utf-8', newline='\n')
groups = {Path(e['physical']).parent for e in selection['inputs']}
blcs = []
for group in sorted(groups):
    blc = group / (group.name + '.blc')
    blcs.append(dict(file='VFS/' + group.name + '/' + blc.name, sha256=hashlib.sha256(blc.read_bytes()).hexdigest().upper()))
manifest = dict(schema=1, mod='si', enabled=True, mode='android-precompiled-startup', source_blcs=blcs,
                prewarm_sha256=hashlib.sha256((output / 'prewarm.txt').read_bytes()).hexdigest().upper(),
                payloads=payloads, routes_sha256=hashlib.sha256((output / 'routes.tsv').read_bytes()).hexdigest().upper(),
                limitations=['Fixed author defaults; no hot reload or dynamic style/shape controls in this build.'])
(output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('Device pack:', output, 'routes:', len(payloads), 'bytes:', sum(e['bytes'] for e in payloads))
