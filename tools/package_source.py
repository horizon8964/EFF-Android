"""Create an allowlisted source-only GitHub upload ZIP; no device/game data."""
import hashlib
import json
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOP = ('.gitignore', 'README.md', 'LICENSE', 'BUILD.txt', '使用说明.txt', 'THIRD_PARTY_NOTICES.txt')
TREES = ('android', 'tools', 'third_party')
EXCLUDED = {'.git', '.gradle', '.cxx', 'build', '__pycache__', '.idea', '.vscode', 'node_modules'}
SUFFIXES = {'.apk', '.aab', '.so', '.dll', '.exe', '.obj', '.o', '.class', '.pyc',
            '.pdb', '.ilk', '.lib', '.exp', '.jks', '.keystore', '.pem', '.p12', '.pfx',
            '.zip', '.7z', '.rar', '.mesh', '.mat', '.tex', '.buf', '.dds', '.blc', '.chk',
            '.sqlite', '.db', '.png', '.jpg', '.jpeg', '.mp4'}


def files():
    result = [ROOT/name for name in TOP]
    for tree in TREES:
        for path in (ROOT/tree).rglob('*'):
            if path.is_symlink():
                raise ValueError('Symlink requires explicit review: '+str(path))
            rel = path.relative_to(ROOT)
            if not path.is_file() or EXCLUDED.intersection(rel.parts):
                continue
            if path.name == 'local.properties' or path.name.startswith('.env') or path.suffix.lower() in SUFFIXES:
                continue
            # Gradle Wrapper is the only binary JAR required in the source tree.
            if path.suffix.lower() == '.jar' and rel.as_posix() != 'android/gradle/wrapper/gradle-wrapper.jar':
                continue
            result.append(path)
    return sorted(result, key=lambda p: p.relative_to(ROOT).as_posix())


def main():
    version = re.search(r'versionName\s*=\s*"([^"]+)"', (ROOT/'android/app/build.gradle.kts').read_text(encoding='utf-8')).group(1)
    output = ROOT/('dist/EFF-Android-'+version+'-GitHub-source.zip')
    output.parent.mkdir(exist_ok=True)
    paths, manifest = files(), {}
    suspicious = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|AKIA[A-Z0-9]{16}')
    prefix = 'EFF-Android-'+version+'/'
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in paths:
            relative = path.relative_to(ROOT).as_posix()
            data = path.read_bytes()
            if suspicious.search(data):
                raise ValueError('Potential credential requires manual review: '+relative)
            manifest[relative] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            archive.writestr(prefix+relative, data)
        archive.writestr(prefix+'SOURCE_MANIFEST.json', json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError('ZIP CRC failed')
        for name, row in manifest.items():
            data = archive.read(prefix+name)
            assert len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256']
        assert prefix+'android/gradle/wrapper/gradle-wrapper.jar' in archive.namelist()
        assert prefix+'third_party/eff/src/eiem_author_core.cpp' in archive.namelist()
        assert prefix+'third_party/eff/tools/nativepack/author_source.py' in archive.namelist()
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_name(output.name+'.sha256').write_text(digest+'  '+output.name+'\n', encoding='ascii')
    print(json.dumps(dict(file=str(output), files=len(manifest), bytes=output.stat().st_size, sha256=digest), ensure_ascii=False))


if __name__ == '__main__':
    main()
