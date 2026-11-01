#!/usr/bin/env python3
"""Bind an internally built AAR to reviewed stable source and local patches."""
# This is an integrity check, not a signature. CI builds the artifact itself
# or reuses an exact-input cache; every consuming job verifies this record.
import hashlib
import json
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def expected(artifact):
    pin = json.loads((ROOT / 'android/core-source.json').read_text())
    with zipfile.ZipFile(artifact) as archive:
        abis = sorted(name.split('/')[1] for name in archive.namelist()
                      if name.startswith('jni/') and name.endswith('/libtrusttunnel_android.so'))
    if abis != sorted(pin['abis']):
        raise ValueError('Android core must contain every pinned ABI')
    return {
        'version': pin['version'], 'commit': pin['commit'], 'abis': abis,
        'patches': {name: sha256(ROOT / name) for name in pin['patches']},
        'builder': sha256(ROOT / 'scripts/build-trusttunnel-android.sh'),
        'recipes': sha256(ROOT / 'scripts/export-patched-boringssl.sh'),
        'sha256': sha256(artifact),
    }


def main():
    mode, path = sys.argv[1:]
    artifact = Path(path)
    manifest = artifact.with_suffix('.json')
    wanted = expected(artifact)
    if mode == 'write':
        manifest.write_text(json.dumps(wanted, indent=2) + '\n')
    elif mode == 'verify':
        if json.loads(manifest.read_text()) != wanted:
            raise ValueError('Android core source, patch or artifact checksum mismatch')
    else:
        raise ValueError('Expected write or verify')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        sys.exit(str(error))
