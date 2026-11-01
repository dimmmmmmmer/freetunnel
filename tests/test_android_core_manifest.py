"""Reject stale patches, modified binaries and incomplete native architectures."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('manifest', ROOT / 'scripts/android-core-manifest.py')
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)


class CoreManifestTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'android').mkdir()
        (self.root / 'scripts').mkdir()
        (self.root / 'patch').write_text('reviewed routing patch')
        (self.root / 'scripts/build-trusttunnel-android.sh').write_text('reviewed builder')
        (self.root / 'scripts/export-patched-boringssl.sh').write_text('pinned recipes')
        self.abis = ['arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64']
        (self.root / 'android/core-source.json').write_text(json.dumps({
            'version': '1.1.11', 'commit': 'a' * 40, 'patches': ['patch'], 'abis': self.abis}))
        original = manifest.ROOT
        manifest.ROOT = self.root
        self.addCleanup(setattr, manifest, 'ROOT', original)
        self.artifact = self.root / 'core.aar'
        self.make_aar(self.abis)

    def make_aar(self, abis, content=b'native library'):
        with zipfile.ZipFile(self.artifact, 'w') as archive:
            for abi in abis:
                archive.writestr('jni/' + abi + '/libtrusttunnel_android.so', content)

    def test_every_architecture_required(self):
        self.assertEqual(sorted(self.abis), manifest.expected(self.artifact)['abis'])
        self.make_aar(['arm64-v8a'])
        with self.assertRaisesRegex(ValueError, 'every pinned ABI'):
            manifest.expected(self.artifact)

    def test_binary_change_detected(self):
        before = manifest.expected(self.artifact)
        self.make_aar(self.abis, b'wrong native library')
        self.assertNotEqual(before, manifest.expected(self.artifact))

    def test_patch_change_detected(self):
        before = manifest.expected(self.artifact)
        (self.root / 'patch').write_text('different patch')
        self.assertNotEqual(before, manifest.expected(self.artifact))

    def test_builder_change_detected(self):
        before = manifest.expected(self.artifact)
        (self.root / 'scripts/build-trusttunnel-android.sh').write_text('different builder')
        self.assertNotEqual(before, manifest.expected(self.artifact))

    def test_recipe_change_detected(self):
        before = manifest.expected(self.artifact)
        (self.root / 'scripts/export-patched-boringssl.sh').write_text('different recipes')
        self.assertNotEqual(before, manifest.expected(self.artifact))

    def test_source_pin_change_detected(self):
        before = manifest.expected(self.artifact)
        pin = self.root / 'android/core-source.json'
        pin.write_text(pin.read_text().replace('a' * 40, 'b' * 40))
        self.assertNotEqual(before, manifest.expected(self.artifact))


if __name__ == '__main__':
    unittest.main()
