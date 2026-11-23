"""A real, loopback-only TrustTunnel endpoint for the Android routing probe."""
from contextlib import contextmanager
import hashlib
import os
from pathlib import Path
import socket
import subprocess  # nosec B404 - Fixed tools and a digest-verified test endpoint, without a shell.
import tempfile
import time


@contextmanager
def endpoint():
    binary = Path(os.environ["INCOMING_ENDPOINT_BIN"]).resolve(strict=True)
    # Verify the executable too, so an overridden environment path cannot
    # execute an arbitrary program. This digest is from the verified official
    # TrustTunnel 1.3.1 linux-x86_64 archive pinned in incoming-routing.yml.
    expected = "70e0349a55d977f6df8b14e476f8897aa2c1d8d5d441c3cba14f2039b8e35767"
    binary_data = binary.read_bytes()
    if hashlib.sha256(binary_data).hexdigest() != expected:
        raise ValueError("Unexpected incoming-probe endpoint executable")
    with tempfile.TemporaryDirectory(prefix="ft-incoming-endpoint-") as directory:
        root = Path(directory)
        # Execute the checked bytes, not a source path that could be replaced
        # between verification and process creation.
        executable = root / "trusttunnel_endpoint"
        executable.write_bytes(binary_data)
        executable.chmod(0o700)
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",  # nosec B603 - Fixed OpenSSL arguments and private temporary paths.
                        "-keyout", str(root / "key.pem"), "-out", str(root / "cert.pem"),
                        "-days", "1", "-subj", "/CN=vpn.invalid", "-addext", "subjectAltName=DNS:vpn.invalid"],
                       check=True, capture_output=True)
        (root / "credentials.toml").write_text('[[client]]\nusername = "test"\npassword = "probe-only"\n')
        (root / "hosts.toml").write_text('[[main_hosts]]\nhostname = "vpn.invalid"\n'
                                        'cert_chain_path = "cert.pem"\nprivate_key_path = "key.pem"\n')
        (root / "vpn.toml").write_text('listen_address = "127.0.0.1:29443"\n'
                                      'ipv6_available = false\nallow_private_network_connections = true\n'
                                      'credentials_file = "credentials.toml"\n'
                                      '[listen_protocols.http2]\n[forward_protocol]\ndirect = {}\n')
        with Path("incoming-android-endpoint.log").open("w") as log:
            process = subprocess.Popen([str(executable), "vpn.toml", "hosts.toml", "-l", "debug"],  # nosec B603 - Verified private copy, fixed argv.
                                       cwd=root, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 10
                while True:
                    assert process.poll() is None, "Endpoint exited: see incoming-android-endpoint.log"
                    try:
                        with socket.create_connection(("127.0.0.1", 29443), timeout=0.2):
                            break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise AssertionError("Endpoint did not listen")
                        time.sleep(0.1)
                yield root / "cert.pem"
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
