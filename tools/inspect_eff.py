"""Read-only EFF format-2 payload integrity audit; does not prove game compatibility."""
import argparse
import configparser
import hashlib
import json
import struct
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, count):
        require(0 <= count <= len(self.data) - self.pos, "truncated field")
        value = self.data[self.pos:self.pos + count]
        self.pos += count
        return value

    def number(self):
        return struct.unpack('<Q', self.take(8))[0]

    def string(self):
        value = self.take(self.number()).decode('utf-8')
        require('\0' not in value, 'embedded NUL')
        return value


def resource(path, expected):
    data = path.read_bytes()
    require(len(data) >= 80 and data[:6] == b'EFFRES' and data[7] == 0, 'invalid resource header')
    kind, count, directory = struct.unpack_from('<III', data, 12)
    require(kind == expected, 'resource kind mismatch')
    require(struct.unpack_from('<Q', data, 40)[0] == len(data), 'resource size mismatch')
    require(hashlib.sha256(data[80:]).digest() == data[48:80], 'resource body digest mismatch')
    require(directory <= len(data) - 80 and count <= directory // 51, 'invalid directory size')
    reader, blocks, end = Reader(data[80:80 + directory]), {}, 80 + directory
    for _ in range(count):
        name = reader.take(struct.unpack('<H', reader.take(2))[0]).decode('utf-8')
        require(name and '\0' not in name and name not in blocks, 'invalid/duplicate block name')
        offset, size, digest = reader.number(), reader.number(), reader.take(32)
        require(offset == (end + 15) & ~15, 'block alignment/overlap mismatch')
        require(offset <= len(data) and size <= len(data) - offset, 'block out of bounds')
        require(not any(data[end:offset]), 'nonzero padding')
        payload = data[offset:offset + size]
        require(hashlib.sha256(payload).digest() == digest, 'block digest mismatch: ' + name)
        blocks[name], end = payload, offset + size
    require(reader.pos == directory and end == len(data), 'unreferenced resource data')
    result = {'file': path.name, 'kind': kind, 'bytes': len(data), 'blocks': list(blocks)}
    if kind == 4:
        require(set(blocks) == {'pixels', '@texture'}, 'unsupported texture metadata layout')
        require(len(blocks['@texture']) == 20, 'invalid texture metadata size')
        _, width, height, encoding, mips = struct.unpack('<IIIII', blocks['@texture'])
        require(width and height and encoding == 4 and 0 < mips <= max(width, height).bit_length(), 'invalid texture metadata')
        expected_size = sum(max(1, width >> i) * max(1, height >> i) * 4 for i in range(mips))
        require(len(blocks['pixels']) == expected_size, 'texture pixel length mismatch')
        result.update(width=width, height=height, mips=mips, encoding='RGBA32', pixel_bytes=expected_size)
    return result


def audit(root):
    root = root.resolve()
    ini = configparser.ConfigParser(interpolation=None, strict=True)
    ini.optionxform = str
    ini.read_string((root / 'mod.ini').read_text(encoding='utf-8-sig'))
    require(ini['Mod']['format'] == '2', 'unsupported Mod format')
    resources, errors, visited = [], [], set()
    counts = {prefix: sum(s.startswith(prefix) for s in ini.sections()) for prefix in ('Mesh', 'Material', 'Texture', 'Part', 'Replace')}
    for section in ini.sections():
        row = ini[section]
        try:
            for prefix, kind in [('Mesh', 1), ('Material', 2), ('Texture', 4)]:
                if section.startswith(prefix):
                    path = (root / row['path']).resolve()
                    require(path.is_relative_to(root), 'resource path escapes Mod')
                    if path not in visited:
                        resources.append(resource(path, kind))
                        visited.add(path)
            references = []
            if section.startswith('Part'):
                references += [(row['mesh'], 'Mesh')]
                if 'material' in row:
                    references += [(row['material'], 'Material')]
            if section.startswith('Material'):
                references += [(value, 'Texture') for key, value in row.items() if key.startswith('texture.')]
            if section.startswith('Replace') and 'parts' in row:
                references += [(value.strip(), 'Part') for value in row['parts'].split(',')]
            for target, prefix in references:
                require(target in ini and target.startswith(prefix), 'missing/wrong reference: ' + target)
        except (ValueError, KeyError, OSError, struct.error) as error:
            errors.append(section + ': ' + str(error))
    binary = {}
    for name in ('source-inputs.bin', 'compiled.bin'):
        if name == 'compiled.bin' and not (root / name).exists():
            binary[name] = {'present': False, 'optional': True}
            continue
        try:
            data = (root / name).read_bytes()
            require(len(data) >= 40, 'binary header truncated')
            valid_digest = hashlib.sha256(data[:-32]).digest() == data[-32:]
            require(valid_digest, 'binary trailing SHA256 mismatch')
            binary[name] = {'bytes': len(data), 'structure': Reader(data[:-32]).string(), 'sha256_verified': True}
        except (ValueError, OSError) as error:
            errors.append(name + ': ' + str(error))
    return {'mod_id': ini['Mod']['id'], 'format': 2, 'section_counts': counts,
            'unique_resource_files': len(resources), 'resources': resources, 'binaries': binary,
            'texture_pixel_bytes': sum(r.get('pixel_bytes', 0) for r in resources),
            'errors': errors, 'payload_integrity_passed': not errors,
            'limits': 'Checks resource containers, references and outer binary hashes only. Does not decode compiled recipes or prove Android resource/game compatibility.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mod', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = audit(args.mod)
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + '\n', encoding='utf-8')
    print(rendered)
    raise SystemExit(0 if report['payload_integrity_passed'] else 1)
