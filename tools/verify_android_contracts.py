"""Check selected EFF loading method signatures against a real device probe.

These are metadata contracts, not proof of ARM64 calling conventions or game visuals.
"""
import argparse
import json
from pathlib import Path


def verify(probe):
    classes = {(c['namespace'], c['name']): c for c in probe.get('classes', []) if c.get('found')}
    checks = []

    def method(space, klass, name, parameters, returns, static):
        matches = [m for m in classes.get((space, klass), {}).get('methods', [])
                   if m['name'] == name and m['parameters'] == parameters and m['returns'] == returns and m.get('static') == static]
        checks.append(dict(class_name=space + '.' + klass, method=name, parameters=parameters,
                           returns=returns, static=static, exact_matches=len(matches), passed=len(matches) == 1))

    for name, returns in [('LoadBundleFromFile', 'UnityEngine.AssetBundle'), ('LoadBundleFromFileAsync', 'UnityEngine.AssetBundleCreateRequest')]:
        method('Beyond.VFS', 'VirtualFileSystem', name, ['Beyond.Resource.StringPathHash'], returns, False)
        method('Beyond.VFS', 'VirtualFileSystem', name, ['Beyond.Resource.StringPathHash', 'Beyond.VFS.EFileLoaderPosType&', 'System.UInt32'], returns, False)
    for parameters in [['System.String'], ['Beyond.Resource.StringPathHash'], ['Beyond.Resource.StringPathHash', 'Beyond.VFS.EFileLoaderPosType&']]:
        method('Beyond.VFS', 'VirtualFileSystem', 'GetAssetStream', parameters, 'System.IO.Stream', False)
    method('Beyond.Resource', 'StringPathHash', 'get_path', [], 'System.String', False)
    method('Beyond.Resource', 'StringPathHashBinary', 'GetMappingStrByHash', ['System.Int64', 'System.String&'], 'System.Boolean', False)
    for name, returns in [('ReadAssetBundle', 'UnityEngine.AssetBundle'), ('ReadAssetBundleAsync', 'UnityEngine.AssetBundleCreateRequest')]:
        method('Beyond.VFS', 'UnityFileLoaderHelper', name, ['System.String', 'System.String', 'System.UInt64', 'Beyond.VFS.EFileLoaderPosType', 'System.UInt32'], returns, True)
    for klass, prefix in [('UnityStreamingFileHelper', 'ReadStreamAssetBundle'), ('UnityPersistFileHelper', 'ReadPersistAssetBundle')]:
        for suffix, returns in [('', 'UnityEngine.AssetBundle'), ('Async', 'UnityEngine.AssetBundleCreateRequest')]:
            method('Beyond.VFS', klass, prefix + suffix, ['System.String', 'System.String', 'System.UInt64', 'System.UInt32'], returns, True)
    for name, returns in [('LoadFromFile', 'UnityEngine.AssetBundle'), ('LoadFromFileAsync', 'UnityEngine.AssetBundleCreateRequest')]:
        method('UnityEngine', 'AssetBundle', name, ['System.String', 'System.UInt32', 'System.UInt64'], returns, True)
    layouts = []
    for space, name, size, alignments, types in [('Beyond.Resource', 'StringPathHash', 8, [1, 8], ['System.Int64', 'System.UInt64']),
                                               ('Beyond.VFS', 'EFileLoaderPosType', 1, [1], ['System.Byte'])]:
        c = classes.get((space, name), {})
        fields = c.get('instance_fields', [])
        passed = (c.get('type_kind') == 0x11 and c.get('value_size') == size and c.get('alignment') in alignments
                  and len(fields) == 1 and fields[0]['type'] in types and fields[0]['boxed_offset'] == 16)
        layouts.append(dict(type=space + '.' + name, value_size=c.get('value_size'), alignment=c.get('alignment'), fields=fields, passed=passed))
    passed = (probe.get('stage') == 'metadata_probe' and probe.get('missing_exports') == []
              and all(c['passed'] for c in checks) and all(c['passed'] for c in layouts))
    return dict(build=probe.get('build'), pid=probe.get('pid'), metadata_contracts_passed=passed,
                methods=checks, data_layouts=layouts, checked_method_count=len(checks),
                matched_method_count=sum(c['passed'] for c in checks),
                limits='Metadata and data-layout checks only. Native detours, ARM64 aggregate passing, source mappings and model replacement remain unverified.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--output', type=Path, default=Path('reports/android-contracts.json'))
    args = parser.parse_args()
    report = verify(json.loads(args.probe.read_text(encoding='utf-8')))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('methods', 'data_layouts')}, indent=2))
    raise SystemExit(0 if report['metadata_contracts_passed'] else 1)
