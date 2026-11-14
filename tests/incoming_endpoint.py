"""A real, loopback-only TrustTunnel endpoint for the Android routing probe."""
from contextlib import contextmanager
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


@contextmanager
def endpoint():
    binary = Path(os.environ["INCOMING_ENDPOINT_BIN"]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="ft-incoming-endpoint-") as directory:
        root = Path(directory)
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
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
            process = subprocess.Popen([str(binary), "vpn.toml", "hosts.toml", "-l", "debug"],
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
