"""Create a VFS snapshot and fill missing CHKs from an installed asset-pack APK.

Existing CHKs are hard-linked on the same volume; BLCs are copied. Inputs are
never overwritten. APK additions must match current BLC size and MD5 proofs.
"""
import argparse
import hashlib
import json
import os
import shutil
import zipfile
from pathlib import Path


def metadata_snapshot(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*.blc'))}


def complete(source, output, apk, catalog_report, report_path):
    source, output = source.resolve(), output.resolve()
    if output.exists() or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('output must be a new, separate directory')
    catalog = json.loads(catalog_report.read_text(encoding='utf-8'))
    if Path(catalog['root']).resolve() != source or catalog['parse_errors']:
        raise ValueError('catalog is not a parsed index of this source directory')
    baseline = metadata_snapshot(source)
    if set(baseline) != {Path(c['blc']).relative_to(source).as_posix() for c in catalog['catalogs']}:
        raise ValueError('BLC set changed since the catalog was created; audit again')
    expected = {}
    for group in catalog['catalogs']:
        for entry in group['chunks']:
            path = Path(entry['file']).resolve()
            relative = path.relative_to(source).as_posix()
            identity = (entry['declared_bytes'], entry['md5'].lower())
            if relative in expected and expected[relative] != identity:
                raise ValueError('conflicting current chunk content: ' + relative)
            expected[relative] = identity
    output.mkdir(parents=True)
    linked = 0
    for p in sorted(source.rglob('*')):
        if not p.is_file() or p.suffix.lower() not in ('.chk', '.blc'):
            continue
        destination = output / p.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if p.suffix.lower() == '.blc':
            shutil.copy2(p, destination)
        else:
            os.link(p, destination)
            linked += 1
    additions, mismatches = [], []
    with zipfile.ZipFile(apk) as archive:
        for info in archive.infolist():
            if not info.filename.startswith('assets/VFS/') or not info.filename.endswith('.chk'):
                continue
            relative = info.filename.removeprefix('assets/VFS/')
            if relative not in expected:
                continue
            target = (output / relative).resolve()
            if not target.is_relative_to(output):
                raise ValueError('APK path escapes output')
            if target.exists():
                continue
            size, digest = expected[relative]
            if info.file_size != size:
                mismatches.append(dict(file=relative, reason='APK length differs from current BLC'))
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + '.partial')
            actual = hashlib.md5()
            written = 0
            print('Recovering', relative, size, 'bytes', flush=True)
            with archive.open(info) as stream, temporary.open('xb') as out:
                while block := stream.read(8 * 1024 * 1024):
                    actual.update(block)
                    out.write(block)
                    written += len(block)
            if written != size or actual.hexdigest() != digest:
                raise ValueError('recovered CHK content does not match current BLC: ' + relative)
            # On Windows rename refuses an existing destination; no input files are replaced.
            os.rename(temporary, target)
            additions.append(dict(file=relative, bytes=written, md5=digest, verified=True))
    stable = baseline == metadata_snapshot(source) == metadata_snapshot(output)
    remaining = [name for name in expected if not (output / name).is_file()]
    report = dict(source=str(source), output=str(output), apk=str(apk.resolve()),
                  existing_chunks_hardlinked=linked, recovered_chunks=additions,
                  recovered_bytes=sum(a['bytes'] for a in additions),
                  source_metadata_unchanged=stable, remaining_missing_chunks=remaining,
                  mismatches=mismatches,
                  limits='Existing CHKs share disk storage with the source; do not edit their contents. Optional/uninstalled resources may remain absent. Run the VFS audit against the completed snapshot to verify Bundle coverage.')
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('recovered_chunks', 'remaining_missing_chunks')}, ensure_ascii=False, indent=2))
    if not stable:
        raise RuntimeError('source metadata changed during recovery; do not treat output as a stable snapshot')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--catalog', type=Path, default=Path('reports/mobile-vfs-current.json'))
    parser.add_argument('--report', type=Path, default=Path('reports/mobile-vfs-completion.json'))
    args = parser.parse_args()
    complete(args.source, args.output, args.apk, args.catalog, args.report)
