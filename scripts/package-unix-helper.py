#!/usr/bin/env python3
"""Package the deployed runtime for independently verified helper updates."""
import argparse
from pathlib import Path
import tarfile


def package(source: Path, output: Path, platform: str) -> None:
    source = source.resolve(strict=True)
    executable = source / ("Contents/MacOS/FreeTunnel" if platform == "macos" else "bin/FreeTunnel")
    if not executable.is_file():
        raise ValueError(f"Missing deployed executable: {executable}")
    for path in source.rglob("*"):
        if path.is_symlink() and not path.resolve(strict=True).is_relative_to(source):
            raise ValueError(f"Runtime link escapes the deployed bundle: {path}")
        if not (path.is_symlink() or path.is_file() or path.is_dir()):
            raise ValueError(f"Unsupported runtime entry: {path}")

    def owner(entry: tarfile.TarInfo) -> tarfile.TarInfo:
        entry.uid = entry.gid = 0
        entry.uname = entry.gname = "root"
        entry.mode &= 0o755
        return entry

    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "w:gz", format=tarfile.PAX_FORMAT) as archive:
        archive.add(source, arcname="runtime/FreeTunnel.app" if platform == "macos" else "runtime", filter=owner)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("platform", choices=("linux", "macos"))
    args = parser.parse_args()
    package(args.source, args.output, args.platform)
