#!/usr/bin/env python3
"""Build the pinned, patched desktop Qt. No unpatched SDK is used as a fallback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess  # nosec B404 - Only allowlisted build tools, argument arrays, no shell.
import tarfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MODULES = frozenset({'qtbase', 'qtshadertools', 'qtsvg', 'qtdeclarative', 'qttools', 'qttranslations'})


def run(command, *args):
    if command not in {'git', 'cmake'}:
        raise ValueError('Only git and cmake are permitted build tools')
    executable = shutil.which(command)
    if executable is None:
        raise RuntimeError(f'{command}: build tool not found')
    # Paths are separate argv entries, never shell syntax; the executable is allowlisted.
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args
    subprocess.run([executable, *map(str, args)], check=True, shell=False)  # nosec B603


def archive_url(module, pin):
    if module not in MODULES:
        raise ValueError('Unknown Qt module')
    if not re.fullmatch(r'[0-9a-f]{40}', pin['commit']):
        raise ValueError('Qt source must use an immutable commit')
    if not re.fullmatch(r'[0-9a-f]{64}', pin['sha256']):
        raise ValueError('Qt source must have a SHA-256 digest')
    url = f"https://codeload.github.com/qt/{module}/tar.gz/{pin['commit']}"
    if pin['url'] != url:
        raise ValueError('Qt source URL must match the pinned official HTTPS archive')
    return url


def source(module, pin, workspace):
    url = archive_url(module, pin)
    archive = workspace / (module + '.tar.gz')
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != pin['sha256']:
        for attempt in range(3):
            try:
                # archive_url permits only the exact official HTTPS URL; the digest is checked below.
                with urllib.request.urlopen(url, timeout=120) as response, archive.open('wb') as target:  # nosec B310
                    shutil.copyfileobj(response, target)
                break
            except OSError:
                if attempt == 2:
                    raise
                time.sleep(3)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != pin['sha256']:
        raise RuntimeError(f'{module}: source checksum mismatch')
    directory = workspace / (module + '-' + pin['commit'])
    patch = ROOT / 'vendor/qt' / (module + '.patch')
    fingerprint = hashlib.sha256(
        pin['sha256'].encode() + (patch.read_bytes() if patch.exists() else b'')
    ).hexdigest()
    if not directory.exists():
        # Hash-verified GitHub source archives, extracted with tar's path filter.
        with tarfile.open(archive) as package:
            package.extractall(workspace, filter='data')
        if patch.exists():
            run('git', '-C', directory, 'apply', '--check', patch)
            run('git', '-C', directory, 'apply', patch)
        (directory / '.freetunnel-patched').write_text(fingerprint)
    marker = directory / '.freetunnel-patched'
    if not marker.exists() or marker.read_text() != fingerprint:
        raise RuntimeError(f'{module}: incomplete or stale sources; remove {directory} and retry')
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 12))
    args = parser.parse_args()
    prefix, workspace = args.prefix.resolve(), args.work.resolve()
    workspace.mkdir(parents=True, exist_ok=True)
    pin = json.loads((ROOT / 'vendor/qt/source.json').read_text())
    system = platform.system()
    common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
              f'-DCMAKE_INSTALL_PREFIX={prefix}', f'-DCMAKE_PREFIX_PATH={prefix}',
              '-DQT_BUILD_EXAMPLES=OFF', '-DQT_BUILD_TESTS=OFF',
              '-DQT_BUILD_BENCHMARKS=OFF', '-DBUILD_WITH_PCH=OFF']
    if system == 'Darwin':
        common += ['-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64',
                   '-DCMAKE_OSX_DEPLOYMENT_TARGET=' + pin['macos_deployment_target']]
    for module, module_pin in pin['modules'].items():
        directory = source(module, module_pin, workspace)
        build = workspace / ('build-' + module)
        options = []
        if module == 'qtbase':
            options += ['-DFEATURE_sql=OFF']
            if system == 'Linux':
                options += ['-DFEATURE_xcb=ON', '-DFEATURE_dbus=ON',
                            '-DFEATURE_openssl=ON', '-DFEATURE_openssl_runtime=ON']
            else:
                # Native Schannel / Secure Transport avoids a second TLS runtime.
                options += ['-DFEATURE_openssl=OFF', '-DFEATURE_icu=OFF',
                            '-DFEATURE_system_freetype=OFF', '-DFEATURE_system_harfbuzz=OFF',
                            '-DFEATURE_system_pcre2=OFF', '-DFEATURE_system_png=OFF',
                            '-DFEATURE_system_jpeg=OFF', '-DFEATURE_system_zlib=OFF']
        elif module == 'qttools':
            options += ['-DFEATURE_assistant=OFF', '-DFEATURE_designer=OFF',
                        '-DFEATURE_clang=OFF', '-DFEATURE_clangcpp=OFF']
        run('cmake', '-S', directory, '-B', build, *common, *options)
        run('cmake', '--build', build, '--parallel', args.jobs)
        run('cmake', '--install', build)
    shutil.copyfile(ROOT / 'vendor/qt/source.json', prefix / 'freetunnel-qt-source.json')


if __name__ == '__main__':
    main()
