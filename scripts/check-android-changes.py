#!/usr/bin/env python3
"""Run Android CI for affected PRs; uncertainty always runs the tests."""
import fnmatch
import os
import subprocess

PATTERNS = (
    'android/*', 'include/core/ReleaseSigning.h', 'THIRD_PARTY_NOTICES.md',
    'LICENSE', 'licenses/*', 'qml/*', 'assets/icons/*', 'assets/logo.svg',
    'assets/fonts/*', 'i18n/freetunnel_ru.ts', 'data/*',
    '.github/workflows/android*.yml', 'scripts/*android*',
    'scripts/check-apk-*.sh', 'tests/test_android_*',
)


def affected(paths):
    return not paths or any(fnmatch.fnmatchcase(path, pattern) for path in paths for pattern in PATTERNS)


def main():
    run = True
    if os.environ.get('GITHUB_EVENT_NAME') == 'pull_request':
        base, head = os.environ.get('PR_BASE_SHA', ''), os.environ.get('PR_HEAD_SHA', '')
        if base and head:
            try:
                # Include both sides of renames. A shallow PR merge checkout has
                # both parents; missing objects fail closed to a full Android run.
                output = subprocess.check_output(['git', 'diff', '--name-only', '--no-renames', '-z', base, head])
                paths = output.decode('utf-8').rstrip('\0').split('\0') if output else []
                run = affected(paths)
            except (subprocess.CalledProcessError, UnicodeError):
                pass
    value = str(run).lower()
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write('android=' + value + '\n')
    print('android=' + value)


if __name__ == '__main__':
    main()
