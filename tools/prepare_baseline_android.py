"""Select actual Android VFS inputs from one preserved EFFSOURCE prefab.

Unlike Windows reverse-consumer discovery, this covers only this explicit
prefab baseline and its exported dependency closure. Native compilation still
verifies the concrete Mesh/renderer relationships before publishing anything.
"""
import argparse
import hashlib
import json
import sqlite3
from pathlib import Path
from prepare_si_android import write_selection


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--database',type=Path,default=Path('reports/mobile-vfs-completed.sqlite'))
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    raw=(args.baseline/'source/manifest.json').read_bytes();m=json.loads(raw.decode('utf-8-sig'))
    if m.get('format')!='EFFSOURCE':raise ValueError('requires EFFSOURCE manifest')
    logical=lambda s:s.replace('\\','/').lower()
    bundles={logical(s) for s in m['closure']}
    if not bundles or any(not s.startswith('bundles/android/') for s in bundles):raise ValueError('requires Android baseline closure')
    owners={r['cab'].lower():logical(r['sourceBundle']) for r in m['serializedFiles']}
    dependencies={s:set() for s in bundles}
    for row in m['serializedFiles']:
        src=logical(row['sourceBundle'])
        if src not in bundles:raise ValueError('snapshot source outside closure')
        for ref in row['externals']:
            dest=owners.get(ref['cab'].lower())
            if dest and dest!=src:dependencies[src].add(dest)
    assets={logical(m['prefab']):logical(m['sourceBundle'])}
    for r in m['resources']:
        if r['type'] not in ('Mesh','Material','Texture2D') or not r.get('logicalPath'):continue
        path,src=logical(r['logicalPath']),logical(r['sourceBundle'])
        if src not in bundles:raise ValueError('resource outside source closure')
        if path in assets and assets[path]!=src:raise ValueError('resource path has multiple source bundles')
        assets[path]=src
    value=dict(manifest=hashlib.sha256(raw).hexdigest().upper(),assets=assets,inputs=[],
               dependencies={k:sorted(v) for k,v in dependencies.items()},candidates=[logical(m['sourceBundle'])])
    with sqlite3.connect(args.database) as db:
        for src in sorted(bundles):
            rows=db.execute('SELECT physical,offset,size,md5,encrypted,available FROM files WHERE logical=?',(src,)).fetchall()
            if len(rows)!=1:raise ValueError('Android source missing or ambiguous: '+src)
            physical,offset,size,md5,encrypted,available=rows[0]
            if encrypted or not available:raise ValueError('Android source unavailable: '+src)
            path=Path(physical);stamp=path.stat()
            with path.open('rb') as stream:stream.seek(offset);data=stream.read(size)
            if len(data)!=size or hashlib.md5(data).hexdigest()!=md5:raise ValueError('VFS range MD5 differs: '+src)
            if (stamp.st_size,stamp.st_mtime_ns)!=(path.stat().st_size,path.stat().st_mtime_ns):raise ValueError('VFS changed during read')
            value['inputs'].append(dict(logical=src,physical=str(path.resolve()),offset=offset,size=size,sha256=hashlib.sha256(data).hexdigest().upper()))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(write_selection(value))
    print('Android preserved-prefab selection: inputs=',len(value['inputs']),'candidates=1; reverse consumers outside this baseline are not included')


if __name__=='__main__':main()
