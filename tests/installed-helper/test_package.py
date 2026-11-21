"""Ensure the runtime archive stays self-contained and has safe permissions."""
import importlib.util
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "package_unix_helper", Path(__file__).resolve().parents[2] / "scripts/package-unix-helper.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTest(unittest.TestCase):
    def test_deployed_runtime_and_internal_links(self):
        for platform, relative, prefix in [("linux", "bin/FreeTunnel", "runtime"),
                                           ("macos", "Contents/MacOS/FreeTunnel", "runtime/FreeTunnel.app")]:
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source = root / "source"
                executable = source / relative
                executable.parent.mkdir(parents=True)
                executable.write_bytes(b"test executable")
                executable.chmod(0o6777)
                (source / "link").symlink_to(relative)
                output = root / "helper.tar.gz"
                packager.package(source, output, platform)
                with tarfile.open(output) as archive:
                    self.assertEqual(archive.extractfile(f"{prefix}/{relative}").read(), b"test executable")
                    self.assertEqual(archive.getmember(f"{prefix}/link").linkname, relative)
                    for member in archive:
                        self.assertEqual((member.uid, member.gid), (0, 0))
                        self.assertEqual(member.mode & 0o7022, 0)
                        self.assertTrue(member.name == prefix or member.name.startswith(prefix + "/"))

    def test_rejects_escaping_link(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            (source / "bin").mkdir(parents=True)
            (source / "bin/FreeTunnel").write_bytes(b"test")
            (root / "outside").write_bytes(b"not part of the runtime")
            (source / "link").symlink_to("../outside")
            with self.assertRaisesRegex(ValueError, "escapes"):
                packager.package(source, root / "helper.tar.gz", "linux")

    def test_rejects_missing_executable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "Missing deployed executable"):
                packager.package(root, root / "helper.tar.gz", "linux")


if __name__ == "__main__":
    unittest.main()
