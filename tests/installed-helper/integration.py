#!/usr/bin/env python3
"""Exercise the actual privileged installer only in a disposable Docker rootfs."""
import hashlib
import fcntl
import os
from pathlib import Path
import shutil
import subprocess  # nosec B404 - Deliberately exercise a test binary in the guarded disposable container.
import sys
import tarfile
import tempfile
import time

if os.geteuid() != 0 or os.environ.get("FT_HELPER_CONTAINER_TEST") != "1" or not Path("/.dockerenv").exists():
    sys.exit("Run only in a disposable Docker container, with FT_HELPER_CONTAINER_TEST=1")

fixture = Path(sys.argv[1]).resolve()
key = Path(sys.argv[2]).resolve()
root = Path("/opt/freetunnel-helper")
installed = root / "current/runtime/bin/FreeTunnel"
assert not root.exists(), "The container must have no installed helper"


def run(*args, success=True, env=None):
    # The fixture supplied by the test runner is intentionally executed as root
    # only after the disposable-container guard. No application input reaches it.
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-tainted-env-args.dangerous-subprocess-use-tainted-env-args
    result = subprocess.run([str(arg) for arg in args], capture_output=True, text=True, env=env)  # nosec B603 - Test-controlled argv, no shell.
    assert (result.returncode == 0) == success, (args, result.returncode, result.stdout, result.stderr)
    return result


def as_user(user, *args, success=True):
    return run("runuser", "-u", user, "--", "sudo", "-n", "--", installed,
               "--installed-helper", *args, success=success)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


run("useradd", "-u", "1001", "-m", "helper-a")
run("useradd", "-u", "1002", "-m", "helper-b")

with tempfile.TemporaryDirectory() as temporary:
    work = Path(temporary)
    work.chmod(0o755)
    payload = work / "payload/runtime/bin/FreeTunnel"
    payload.parent.mkdir(parents=True)
    shutil.copy2(fixture, payload)

    def package(version, name, extra=b""):
        payload.write_bytes(fixture.read_bytes() + extra)
        payload.chmod(0o755)
        archive = work / (name + ".tar.gz")
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(payload.parents[1], arcname="runtime")
        sums = work / (name + ".sums")
        sums.write_text(f"#version={version}\n{digest(archive)}  freetunnel-helper-linux-x86_64.tar.gz\n")
        signature = work / (name + ".sig")
        run("openssl", "pkeyutl", "-sign", "-rawin", "-inkey", key, "-in", sums, "-out", signature)
        return [archive, sums, signature, digest(payload), "1001"]

    initial = package("1.2.5", "initial")
    redirected = work / "redirected-root"
    redirected.mkdir()
    root.symlink_to(redirected)
    run(fixture, "--install-helper", *initial, success=False)
    assert not list(redirected.iterdir())
    root.unlink()
    corrupted = work / "bad.sig"
    corrupted.write_bytes(b"x" * 64)
    run(fixture, "--install-helper", initial[0], initial[1], corrupted, *initial[3:], success=False)
    assert not installed.exists()
    run(fixture, "--install-helper", *initial,
        env={**os.environ, "TAR_OPTIONS": "--not-a-real-option", "QT_PLUGIN_PATH": str(work)})
    assert digest(installed) == initial[3]
    run("runuser", "-u", "helper-a", "--", "test", "-r", installed)
    run("visudo", "-c")
    as_user("helper-a", "check")
    as_user("helper-b", "check", success=False)
    as_user("helper-a", "remove", success=False)
    as_user("helper-a", "--authorize-helper", "1002", success=False)
    as_user("helper-a", "check", "--helper", success=False)
    # Prove the joined string matches sudoers: rejection must come from our
    # dispatcher, not accidentally from sudo denying the test command.
    run("runuser", "-u", "helper-a", "--", "sudo", "-n", "-l", "--", installed,
        "--installed-helper start", "--helper")
    run("runuser", "-u", "helper-a", "--", "sudo", "-n", "--", installed,
        "--installed-helper start", "--helper", success=False)
    run("runuser", "-u", "helper-a", "--", installed, "--remove-helper", success=False)

    token = work / ".fthelper-a"
    token.write_text("0123456789abcdef0123456789abcdef")
    token.chmod(0o600)
    os.chown(token, 1001, 1001)
    start = ["start", "--port", "49152", "--token-file", str(token)]
    token.chmod(0o644)
    as_user("helper-a", *start, success=False)
    token.chmod(0o600)
    as_user("helper-a", *start, "--helper", success=False)
    other = work / ".fthelper-b"
    other.write_text("abcdef0123456789abcdef0123456789")
    other.chmod(0o600)
    os.chown(other, 1002, 1002)
    as_user("helper-a", *start[:-1], other, success=False)
    link = work / ".fthelper-link"
    link.symlink_to(token)
    as_user("helper-a", *start[:-1], link, success=False)
    hard = work / ".fthelper-hard"
    os.link(token, hard)
    as_user("helper-a", *start, success=False)
    hard.unlink()

    update = package("1.2.6", "update", b"\nnew signed runtime\n")
    active = subprocess.Popen(["runuser", "-u", "helper-a", "--", "sudo", "-n", "--",  # nosec B603 - Fixed test account and verified installed fixture, no shell.
                               str(installed), "--installed-helper", *start])
    deadline = time.monotonic() + 5
    with Path(str(root) + ".lock").open("r") as lock:
        while True:
            assert active.poll() is None
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fcntl.flock(lock, fcntl.LOCK_UN)
            except BlockingIOError:
                break
            assert time.monotonic() < deadline, "Session did not acquire the runtime lock"
            time.sleep(0.02)
    as_user("helper-a", "update", *update, success=False)
    run(installed, "--remove-helper", success=False)
    assert digest(installed) == initial[3]
    assert active.wait(timeout=10) == 0
    old = package("1.2.4", "old")
    as_user("helper-a", "update", *old, success=False)
    as_user("helper-a", "update", *initial, success=False)
    as_user("helper-a", "update", *update[:3], "0" * 64, "1001", success=False)
    escape = payload.parents[1] / "escape"
    escape.symlink_to("/etc")
    bad_tree = package("1.2.6", "escape")
    as_user("helper-a", "update", *bad_tree, success=False)
    escape.unlink()
    assert digest(installed) == initial[3]
    as_user("helper-a", "update", *update)
    assert digest(installed) == update[3]
    as_user("helper-a", *start)
    run(installed, "--authorize-helper", "1002")
    as_user("helper-b", "check")
    run(installed, "--remove-helper")
    assert not root.exists()
    assert not list(Path("/etc/sudoers.d").glob("freetunnel-helper-*"))

    packaged = Path("/opt/freetunnel/bin/FreeTunnel")
    packaged.parent.mkdir(parents=True)
    shutil.copy2(fixture, packaged)
    run(packaged, "--install-packaged-helper", env={**os.environ, "SUDO_UID": "1001"})
    run("runuser", "-u", "helper-a", "--", "test", "-r", installed)
    as_user("helper-a", *start)
    run(installed, "--remove-helper")
    assert not root.exists()

print("PASS: signed installation/update, downgrade/tamper rejection, per-user grants, token ownership, busy runtime, removal, package installation")
