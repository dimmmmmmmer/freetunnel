import importlib.util
import itertools
import os
from pathlib import Path
import subprocess  # nosec B404 - Tests execute repository-owned CI scripts.
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('changes', ROOT / 'scripts/check-android-changes.py')
changes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(changes)


class AndroidCiTest(unittest.TestCase):
    def test_android_and_shared_changes_run(self):
        for path in ['android/app/build.gradle', 'android/app/src/main/A.java',
                     '.github/workflows/android-release.yml', '.github/workflows/android.yml',
                     'scripts/run-android-permission-test.sh', 'scripts/check-apk-data.sh',
                     'scripts/check-android-changes.py', 'tests/test_android_ci.py',
                     'qml/pages/Home.qml', 'data/split/example.txt', 'assets/fonts/Inter.ttf',
                     'include/core/ReleaseSigning.h', 'licenses/android/license.txt', 'THIRD_PARTY_NOTICES.md']:
            self.assertTrue(changes.affected([path]), path)
        self.assertTrue(changes.affected([]))
        self.assertTrue(changes.affected(['README.md', 'android/removed.java']))
        self.assertFalse(changes.affected(['README.md', 'USER_GUIDE.md', 'src/app/Backend.cpp']))

    def test_gate_rejects_every_non_successful_required_job(self):
        workflow = (ROOT / '.github/workflows/android.yml').read_text()
        start = workflow.index('          test "$CHANGES" = success')
        body = workflow[start:].split('\n\n', 1)[0]
        script = '\n'.join(line[10:] for line in body.splitlines())
        for results in itertools.product(['success', 'failure', 'cancelled', 'skipped'], repeat=4):
            env = dict(os.environ, CHANGES='success', ANDROID='true', BUILD=results[0], DEVICES=results[1], SIGNING=results[2], CORE=results[3])
            # Command and script come from this repository, not user input.
            result = subprocess.run(['bash', '-c', script], env=env, capture_output=True)  # nosec B603
            self.assertEqual(all(r == 'success' for r in results), result.returncode == 0, results)
        for changed, expected in [('success', 0), ('failure', 1), ('cancelled', 1)]:
            env = dict(os.environ, CHANGES=changed, ANDROID='false', BUILD='skipped', DEVICES='skipped', SIGNING='skipped')
            # Command and script come from this repository, not user input.
            self.assertEqual(expected, subprocess.run(['bash', '-c', script], env=env, capture_output=True).returncode)  # nosec B603

    def test_core_cache_is_exact_and_disabled_for_releases(self):
        workflow = (ROOT / '.github/workflows/android.yml').read_text()
        core = workflow.split('  core:\n', 1)[1].split('  build:\n', 1)[0]
        self.assertIn('if: inputs.gradle-cache-disabled != true', core)
        self.assertNotIn('restore-keys:', core)
        for source in ['android/core-source.json', 'vendor/trusttunnel/*.patch',
                       'scripts/build-trusttunnel-android.sh', 'scripts/export-patched-boringssl.sh',
                       'scripts/android-core-manifest.py']:
            self.assertIn(source, core)
        self.assertIn('      - run: scripts/prepare-trusttunnel-android.sh\n      - uses: actions/upload-artifact', core)

    def test_instrumentation_exit_and_result_are_both_checked(self):
        with tempfile.TemporaryDirectory() as temporary:
            adb = Path(temporary) / 'adb'
            adb.write_text('#!/bin/sh\nprintf "%s\\n" "$FAKE_OUTPUT"\nexit "$FAKE_STATUS"\n')
            adb.chmod(0o755)
            for output, code, passed in [('OK (1 test)', '0', True), ('OK (1 test)\r', '0', True),
                                         ('FAILURES!!!', '0', False), ('OK (1 test)', '1', False),
                                         ('', '0', False), ('OK (0 tests)', '0', False)]:
                env = dict(os.environ, PATH=temporary + ':' + os.environ['PATH'], FAKE_OUTPUT=output, FAKE_STATUS=code)
                # Command and script come from this repository, not user input.
                result = subprocess.run(['bash', str(ROOT / 'scripts/run-android-permission-test.sh')], env=env, capture_output=True)  # nosec B603
                self.assertEqual(passed, result.returncode == 0, (output, code))
                self.assertIn(output.encode(), result.stdout)


if __name__ == '__main__':
    unittest.main()
