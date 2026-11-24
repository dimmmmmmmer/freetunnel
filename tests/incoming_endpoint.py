"""A real TrustTunnel endpoint restricted to the local test host."""
from contextlib import contextmanager
import hashlib
import ipaddress
import os
from pathlib import Path
import socket
import sys
import subprocess  # nosec B404 - Fixed tools and a digest-verified test endpoint, without a shell.
import tempfile
import time


@contextmanager
def endpoint(socks_port=None, log_path="incoming-android-endpoint.log", address="127.0.0.1", password=None):
    address = str(ipaddress.IPv4Address(address))
    binary = Path(os.environ["INCOMING_ENDPOINT_BIN"]).resolve(strict=True)
    # Verify the executable too, so an overridden environment path cannot
    # execute an arbitrary program. This digest is from the verified official
    # Linux and macOS TrustTunnel 1.3.1 release archives pinned by the workflows.
    expected = ("758ff35ad1d9c936ceebbb2a27b86c26247117ee5540edb4360fcb9a357e66d8" if sys.platform == "darwin"
                else "70e0349a55d977f6df8b14e476f8897aa2c1d8d5d441c3cba14f2039b8e35767")
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
        if password is None:
            if not ipaddress.IPv4Address(address).is_loopback:
                raise ValueError("A non-loopback probe requires a fresh credential")
            # The existing Android instrumentation and DNS fixtures share this
            # credential only with their own loopback server.
            credentials = '[[client]]\nusername = "test"\npassword = "probe-only"\n'
        else:
            credentials = f'[[client]]\nusername = "test"\npassword = "{password}"\n'
        (root / "credentials.toml").write_text(credentials)
        (root / "rules.toml").write_text(f'[[rule]]\ncidr = "{address}/32"\naction = "allow"\n'
                                        '[[rule]]\ncidr = "0.0.0.0/0"\naction = "deny"\n')
        (root / "hosts.toml").write_text('[[main_hosts]]\nhostname = "vpn.invalid"\n'
                                        'cert_chain_path = "cert.pem"\nprivate_key_path = "key.pem"\n')
        forward = (f'[forward_protocol.socks5]\naddress = "127.0.0.1:{socks_port}"\nextended_auth = false\n'
                   if socks_port else "[forward_protocol]\ndirect = {}\n")
        (root / "vpn.toml").write_text(f'listen_address = "{address}:29443"\nrules_file = "rules.toml"\n'
                                      'ipv6_available = false\nallow_private_network_connections = true\n'
                                      'credentials_file = "credentials.toml"\n'
                                      '[listen_protocols.http2]\n' + forward)
        with Path(log_path).open("w") as log:
            process = subprocess.Popen([str(executable), "vpn.toml", "hosts.toml", "-l", "debug"],  # nosec B603 - Verified private copy, fixed argv.
                                       cwd=root, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 10
                while True:
                    assert process.poll() is None, f"Endpoint exited: see {log_path}"
                    try:
                        with socket.create_connection((address, 29443), timeout=0.2):
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
