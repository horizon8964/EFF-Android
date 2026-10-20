"""Read-only Endfield VFS v3/v4/v5 index audit and targeted raw extraction.

The format and ChaCha20 parameters follow EFF 1.8.1 eiem_vfs_catalog.h.
Does not decode Unity objects or modify the input VFS.
"""
import argparse
import hashlib
import json
import sqlite3
import struct
from collections import Counter
from pathlib import Path
from Crypto.Cipher import ChaCha20

KEY = bytes.fromhex('E95B317AC4F828569D23A86BF271DCB53E846FA75C924D671DBA8E38F4CA52E1')


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, count):
        if not 0 <= count <= len(self.data) - self.pos:
            raise ValueError(f'truncated BLC at offset {self.pos}, requested {count}')
        value = self.data[self.pos:self.pos + count]
        self.pos += count
        return value

    def number(self, count):
        return int.from_bytes(self.take(count), 'little')

    def count(self, minimum):
        count = self.number(4)
        if count > (len(self.data) - self.pos) // minimum:
            raise ValueError(f'invalid BLC count {count} at {self.pos - 4}')
        return count


def normalize(raw):
    # Match EFF's normalization of VFS paths with nonprintable decorations.
    text = ''.join(chr(c) for c in raw if 32 <= c <= 126).replace('\\', '/').lower()
    starts = [text.find(prefix) for prefix in ('data/', 'assets/') if prefix in text]
    if starts:
        text = text[min(starts):]
    for prefix in ('assets/streamingassets/', 'assets/', 'data/'):
        if text.startswith(prefix):
            text = text[len(prefix):]
            break
    return text.strip('/')


def catalog(path):
    raw = path.read_bytes()
    if len(raw) < 16:
        raise ValueError('BLC header truncated')
    cipher = ChaCha20.new(key=KEY, nonce=raw[:12])
    cipher.seek(64)  # EFF uses the IETF ChaCha state with initial counter 1.
    data = cipher.decrypt(raw[12:])
    r = Reader(data[:-4])
    first = r.number(4)
    code = first if first < 11 else 3
    if code not in (3, 4, 5):
        raise ValueError(f'unsupported BLC code version {code}')
    version = r.number(4) if first < 11 else first
    label = r.take(r.number(2)).decode('utf-8')
    block = r.number(8)
    r.number(4)
    declared_files = r.number(8)
    r.number(1)
    chunk_count = r.count(45)
    chunks, entries = [], []
    for _ in range(chunk_count):
        chunk_id, chunk_md5 = r.take(16).hex().upper(), r.take(16).hex()
        size, flag = r.number(8), r.number(1)
        if code > 3:
            r.number(8 if code >= 5 else 4)
        physical = path.parent / (chunk_id + '.chk')
        exists = physical.is_file()
        actual = physical.stat().st_size if exists else None
        chunk = dict(file=str(physical), declared_bytes=size, actual_bytes=actual, exists=exists,
                     size_matches=actual == size, md5=chunk_md5, flag=flag)
        chunks.append(chunk)
        for _ in range(r.count(60)):
            name = r.take(r.number(2))
            name_hash = r.number(8)
            r.take(16)
            digest = r.take(16).hex()
            offset, length = r.number(8), r.number(8)
            file_flag, encrypted = r.number(1), r.number(1)
            iv = r.number(8) if encrypted else None
            if code > 3:
                r.number(8 if code >= 5 else 4)
            logical = normalize(name)
            available = exists and offset <= actual and length <= actual - offset
            entries.append((logical, name.decode('utf-8', errors='backslashreplace'), str(physical),
                            offset, length, digest, encrypted, str(iv) if iv is not None else None,
                            version, f'{block:016x}', file_flag, str(name_hash), int(available)))
    return dict(blc=str(path), code_version=code, content_version=version, label=label,
                block=f'{block:016x}', declared_size_field=declared_files, chunk_count=chunk_count,
                entry_count=len(entries), remaining_bytes_before_trailer=len(r.data) - r.pos,
                trailer_hex=data[r.pos:].hex(), chunks=chunks), entries


def audit(root, database, report_path):
    root = root.resolve()
    if database.exists():
        raise FileExistsError(f'refusing to overwrite an existing index: {database}')
    database.parent.mkdir(parents=True, exist_ok=True)
    reports, errors, platforms = [], [], Counter()
    with sqlite3.connect(database) as db:
        db.execute('CREATE TABLE files(logical TEXT, raw_name TEXT, physical TEXT, offset INTEGER, size INTEGER, md5 TEXT, encrypted INTEGER, iv TEXT, version INTEGER, block TEXT, flag INTEGER, name_hash TEXT, available INTEGER)')
        db.execute('CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT)')
        db.execute('INSERT INTO metadata VALUES(?,?)', ('root', str(root)))
        for path in sorted(root.rglob('*.blc')):
            try:
                report, entries = catalog(path)
                db.executemany('INSERT INTO files VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)', entries)
                for e in entries:
                    parts = e[0].split('/')
                    if parts[0] == 'bundles' and len(parts) > 1:
                        platforms[parts[1]] += 1
                reports.append(report)
                print(report['label'], 'v' + str(report['code_version']), len(entries), 'entries', flush=True)
            except (ValueError, OSError, UnicodeError) as error:
                errors.append(dict(blc=str(path), error=str(error)))
        db.execute('CREATE INDEX logical_path ON files(logical)')
        db.execute('CREATE INDEX availability ON files(available)')
        total, available, encrypted = db.execute('SELECT COUNT(*),COALESCE(SUM(available),0),COALESCE(SUM(encrypted != 0),0) FROM files').fetchone()
        bundles, readable_bundles = db.execute("SELECT COUNT(*),COALESCE(SUM(available),0) FROM files WHERE logical LIKE 'bundles/%'").fetchone()
        summary = dict(root=str(root), database=str(database.resolve()), catalog_count=len(reports),
                       indexed_files=total, physically_readable_files=available, missing_or_out_of_range_files=total-available,
                       bundle_count=bundles, physically_readable_bundles=readable_bundles, encrypted_entries=encrypted,
                       bundle_platforms=dict(platforms), missing_chunks=sum(not c['exists'] for r in reports for c in r['chunks']),
                       wrong_size_chunks=sum(c['exists'] and not c['size_matches'] for r in reports for c in r['chunks']),
                       parse_errors=errors, catalogs=reports,
                       limits='BLC parsing and declared file extents only. Full CHK hashes and Unity object decoding are not verified.')
        db.execute('INSERT INTO metadata VALUES(?,?)', ('complete', str(not errors).lower()))
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in summary.items() if k != 'catalogs'}, ensure_ascii=False, indent=2))
    return not errors


def extract(database, logical, output):
    with sqlite3.connect(f'file:{database.resolve().as_posix()}?mode=ro', uri=True) as db:
        rows = db.execute('SELECT physical,offset,size,md5,encrypted FROM files WHERE logical=? AND available=1 ORDER BY version DESC', (logical.lower(),)).fetchall()
    if not rows:
        raise ValueError('logical file absent or not physically readable')
    if len(rows) > 1:
        raise ValueError('multiple catalog records: layer selection must be resolved before extraction')
    physical, offset, size, digest, encrypted = rows[0]
    if encrypted:
        raise ValueError('encrypted entry: raw decryption is not implemented')
    with Path(physical).open('rb') as f:
        f.seek(offset)
        data = f.read(size)
    if len(data) != size or hashlib.md5(data).hexdigest() != digest:
        raise ValueError('payload length/MD5 does not match BLC')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as f:
        f.write(data)
    result = dict(logical=logical, output=str(output.resolve()), size=size, md5_verified=True,
                  header_hex=data[:48].hex(), header_text=repr(data[:48]))
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('audit')
    p.add_argument('root', type=Path)
    p.add_argument('--database', type=Path, default=Path('reports/mobile-vfs.sqlite'))
    p.add_argument('--report', type=Path, default=Path('reports/mobile-vfs-audit.json'))
    p = sub.add_parser('extract')
    p.add_argument('database', type=Path)
    p.add_argument('logical')
    p.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.command == 'audit':
        raise SystemExit(0 if audit(args.root, args.database, args.report) else 1)
    else:
        extract(args.database, args.logical, args.output)
