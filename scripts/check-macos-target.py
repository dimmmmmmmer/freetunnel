#!/usr/bin/env python3
"""Reject a packaged dependency that silently raises the app's macOS minimum."""
import argparse
import pathlib
import plistlib
import re
import subprocess  # nosec B404 - Fixed Apple tools, argument arrays, no shell.


def version(value):
    return tuple((list(map(int, value.split('.'))) + [0, 0])[:3])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=pathlib.Path)
    parser.add_argument('--target', default='12.0')
    args = parser.parse_args()
    args.bundle = args.bundle.resolve(strict=True)
    target = version(args.target)
    with (args.bundle / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if version(info['LSMinimumSystemVersion']) != target:
        raise SystemExit('Bundle minimum does not match the supported macOS version')
    checked = 0
    seen = set()
    for path in args.bundle.rglob('*'):
        if not path.is_file() or path.resolve() in seen:
            continue
        seen.add(path.resolve())
        # Absolute bundle paths cannot become options; no content is evaluated as code.
        description = subprocess.check_output(['/usr/bin/file', '-b', str(path)], text=True)  # nosec B603
        if 'Mach-O' not in description:
            continue
        commands = subprocess.check_output(['/usr/bin/otool', '-l', str(path)], text=True)  # nosec B603
        # otool emits one load-command section per architecture of fat binaries.
        minimums = re.findall(r'\bcmd LC_(?:BUILD_VERSION|VERSION_MIN_MACOSX)\b(.*?)(?=Load command|\Z)',
                              commands, re.DOTALL)
        if not minimums:
            raise SystemExit(f'No deployment target in {path}')
        for command in minimums:
            match = re.search(r'\b(?:minos|version)\s+(\d+(?:\.\d+){0,2})\b', command)
            if not match or version(match.group(1)) > target:
                raise SystemExit(f'{path} requires newer macOS than {args.target}: {command.strip()}')
        checked += 1
    if not checked:
        raise SystemExit('No Mach-O binaries found')
    print(f'macOS {args.target}: checked {checked} bundled binaries and all their architectures')


if __name__ == '__main__':
    main()
