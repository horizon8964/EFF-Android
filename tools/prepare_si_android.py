"""Prepare SHA-proven Android inputs for the si EFF author package.

Starts with the author's known Bundle names; the native compiler subsequently
verifies actual Mesh identities, Prefab consumers and dependency closure.
"""
import argparse
import hashlib
import json
import sqlite3
import struct
from pathlib import Path
from compare_android_sources import selection


def write_selection(value):
    out = bytearray()
    def number(n): out.extend(struct.pack('<Q', n))
    def string(s):
        b = s.encode('utf-8'); number(len(b)); out.extend(b)
    def strings(items):
        items = sorted(items); number(len(items))
        for item in items: string(item)
    string('EFF_STATIC_RESOURCE_SELECTION/paths+ranges+dependencies/le64')
    string(value['manifest'])
    number(len(value['assets']))
    for key, source in sorted(value['assets'].items()): string(key); string(source)
    number(len(value['inputs']))
    for e in sorted(value['inputs'], key=lambda e: e['logical']):
        for key in ('logical', 'sha256', 'physical'): string(e[key])
        number(e['offset']); number(e['size'])
    number(len(value['dependencies']))
    for key, dependencies in sorted(value['dependencies'].items()): string(key); strings(dependencies)
    strings(value['candidates'])
    out.extend(hashlib.sha256(out).digest())
    return out


def prepare(mod, database, output):
    original = selection(mod / 'source-inputs.bin')
    mobile = lambda s: s.replace('bundles/windows/', 'bundles/android/')
    value = dict(assets={k: mobile(v) for k, v in original['assets'].items()}, inputs=[],
                 dependencies={mobile(k): [mobile(v) for v in values] for k, values in original['dependencies'].items()},
                 candidates=[mobile(s) for s in original['candidates']])
    with sqlite3.connect(database) as db:
        for e in original['inputs']:
            logical = mobile(e['logical'])
            rows = db.execute('SELECT physical,offset,size,md5,encrypted,available FROM files WHERE logical=?', (logical,)).fetchall()
            if len(rows) != 1: raise ValueError(f'non-unique or missing Android source: {logical}')
            physical, offset, size, md5, encrypted, available = rows[0]
            if encrypted or not available: raise ValueError(f'Android source unavailable/encrypted: {logical}')
            path = Path(physical)
            stamp = path.stat()
            with path.open('rb') as f: f.seek(offset); data = f.read(size)
            if len(data) != size or hashlib.md5(data).hexdigest() != md5: raise ValueError(f'Android source MD5 mismatch: {logical}')
            if (stamp.st_size, stamp.st_mtime_ns) != (path.stat().st_size, path.stat().st_mtime_ns): raise ValueError('source changed while reading')
            value['inputs'].append(dict(logical=logical, sha256=hashlib.sha256(data).hexdigest().upper(), physical=str(path.resolve()), offset=offset, size=size))
    value['manifest'] = hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest().upper()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'android-source-inputs.bin').write_bytes(write_selection(value))
    (output / 'android-source-inputs.json').write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('Android sources:',len(value['inputs']),'assets:',len(value['assets']),'candidates:',len(value['candidates']))
    print('Selected input bytes:',sum(e['size'] for e in value['inputs']))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mod',type=Path,default=Path('si'))
    p.add_argument('--database',type=Path,default=Path('reports/mobile-vfs-completed.sqlite'))
    p.add_argument('--output',type=Path,default=Path('research/si-android'))
    a=p.parse_args();prepare(a.mod,a.database,a.output)
