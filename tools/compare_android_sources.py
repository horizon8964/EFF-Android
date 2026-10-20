"""Compare EFF source-selection file names with a connected Android client's VFS.

This reads metadata only. Matching file names never establish content identity.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from inspect_eff import Reader, require


def selection(path):
    data = path.read_bytes()
    require(len(data) >= 40 and hashlib.sha256(data[:-32]).digest() == data[-32:], 'source selection digest mismatch')
    r = Reader(data[:-32])
    require(r.string() == 'EFF_STATIC_RESOURCE_SELECTION/paths+ranges+dependencies/le64', 'unsupported source selection')
    manifest = r.string()
    assets = {}
    for _ in range(r.number()):
        key, value = r.string(), r.string()
        require(key not in assets, 'duplicate asset')
        assets[key] = value
    inputs = []
    for _ in range(r.number()):
        inputs.append(dict(logical=r.string(), sha256=r.string(), physical=r.string(), offset=r.number(), size=r.number()))
    dependencies = {}
    for _ in range(r.number()):
        key = r.string()
        require(key not in dependencies, 'duplicate dependency owner')
        dependencies[key] = [r.string() for _ in range(r.number())]
    candidates = [r.string() for _ in range(r.number())]
    require(r.pos == len(r.data), 'trailing source selection data')
    logicals = {i['logical'] for i in inputs}
    require(len(logicals) == len(inputs), 'duplicate logical input')
    require(set(assets.values()) <= logicals and set(candidates) <= logicals, 'unresolved source reference')
    require(set(dependencies) <= logicals and all(set(v) <= logicals for v in dependencies.values()), 'unresolved dependency')
    return dict(manifest=manifest, assets=assets, inputs=inputs, dependencies=dependencies, candidates=candidates)


def compare(mod, device_root, adb):
    source = selection(mod / 'source-inputs.bin')
    # Restrict generated remote-shell arguments to known path characters.
    require(re.fullmatch(r'/[A-Za-z0-9_./-]+', device_root) is not None and '..' not in device_root.split('/'), 'unsupported device root')
    probes, unmapped = {}, []
    for entry in source['inputs']:
        match = re.fullmatch(r'Endfield_Data/(?:Persistent|StreamingAssets)/(VFS/[A-Fa-f0-9]{8}/[A-Fa-f0-9]{32}\.chk)', entry['physical'])
        if match:
            probes[entry['physical']] = device_root.rstrip('/') + '/' + match.group(1)
        else:
            unmapped.append(entry['physical'])
    command = [adb, 'shell', 'stat', '-c', '%n:%s', *sorted(set(probes.values()))]
    require(bool(probes), 'no supported VFS source paths')
    process = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
    sizes = {}
    for line in process.stdout.splitlines():
        name, colon, size = line.rpartition(':')
        if colon and size.isdecimal():
            sizes[name] = int(size)
    if process.returncode != 0:
        require('No such file or directory' in process.stderr, 'ADB/stat failed: ' + process.stderr.strip())
    chunks = [dict(pc_path=pc, proposed_android_path=remote, exists=remote in sizes, android_file_bytes=sizes.get(remote)) for pc, remote in sorted(probes.items())]
    return dict(mod=mod.name, source_input_count=len(source['inputs']), declared_asset_count=len(source['assets']),
                candidate_prefab_inputs=len(source['candidates']), source_platforms=sorted({i['logical'].split('/')[1] for i in source['inputs']}),
                unique_chunk_paths=len(chunks), existing_chunk_names=sum(c['exists'] for c in chunks),
                unmapped_paths=sorted(set(unmapped)), chunks=chunks,
                android_content_identity_verified=False,
                conclusion='PC compiled recipes require Android source mapping and content/layout validation; renaming directories alone is insufficient.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mod', type=Path)
    p.add_argument('--device-root', default='/sdcard/Android/data/com.gryphline.endfield.gp/files')
    p.add_argument('--adb', default='adb')
    p.add_argument('--output', type=Path, default=Path('reports/si-android-sources.json'))
    args = p.parse_args()
    report = compare(args.mod, args.device_root, args.adb)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'chunks'}, ensure_ascii=False, indent=2))
