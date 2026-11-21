#!/usr/bin/env python3
"""Observe upstream DNS routing with a real endpoint, inside disposable Docker."""
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess  # nosec B404 - Fixed network tool and digest-verified client, in an isolated container.
import tempfile
import threading
import time

from incoming_endpoint import endpoint


def udp_sender(port):
    """Name the process that sent the datagram, not just the DNS destination."""
    sockets = set()
    for line in Path("/proc/net/udp").read_text().splitlines()[1:]:
        fields = line.split()
        if int(fields[1].rsplit(":", 1)[1], 16) == port:
            sockets.add(f"socket:[{fields[9]}]")
    for process in Path("/proc").iterdir():
        if not process.name.isdecimal():
            continue
        try:
            for descriptor in (process / "fd").iterdir():
                if os.readlink(descriptor) in sockets:
                    return Path(os.readlink(process / "exe")).name
        except (FileNotFoundError, PermissionError):
            continue
    return "unknown"


def serve_dns(port, label, observed):
    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind(("192.0.2.53" if port == 53 else "192.0.2.54", port))

    def receive():
        while True:
            query, peer = server.recvfrom(4096)
            offset, labels = 12, []
            while query[offset]:
                size = query[offset]
                labels.append(query[offset + 1:offset + 1 + size].decode("ascii"))
                offset += size + 1
            name = ".".join(labels)
            observed.append((label, name, udp_sender(peer[1])))
            question = query[12:offset + 5]
            answer = b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 30, 4) + socket.inet_aton("192.0.2.123")
            server.sendto(query[:2] + struct.pack("!HHHHH", 0x8180, 1, 1, 0, 0) + question + answer, peer)

    threading.Thread(target=receive, daemon=True).start()
    return server


def query_dns(name):
    with socket.create_connection(("127.0.0.1", 21080), timeout=3) as control:
        control.sendall(b"\x05\x01\x00")
        assert control.recv(2) == b"\x05\x00"
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(4)
            control.sendall(b"\x05\x03\x00\x01" + socket.inet_aton("127.0.0.1")
                            + struct.pack("!H", udp.getsockname()[1]))
            reply = control.recv(10)
            assert len(reply) == 10 and reply[:4] == b"\x05\x00\x00\x01", reply
            relay = (socket.inet_ntoa(reply[4:8]), struct.unpack("!H", reply[8:])[0])
            qname = b"".join(bytes([len(label)]) + label.encode("ascii") for label in name.split(".")) + b"\0"
            query = struct.pack("!HHHHHH", 1234, 0x100, 1, 0, 0, 0) + qname + struct.pack("!HH", 1, 1)
            udp.sendto(b"\x00\x00\x00\x01" + socket.inet_aton("127.0.0.2")
                       + struct.pack("!H", 53) + query, relay)
            response, _ = udp.recvfrom(4096)
            assert response.endswith(socket.inet_aton("192.0.2.123")), response


def main():
    if os.geteuid() != 0 or os.environ.get("FT_DNS_CONTAINER_TEST") != "1" or not Path("/.dockerenv").exists():
        raise SystemExit("Run only inside the disposable DNS test container")
    binary = Path(os.environ["DNS_CLIENT_BIN"])
    data = binary.read_bytes()
    assert hashlib.sha256(data).hexdigest() == "34efac45a4e52c2b30f2bec266c2f81068d7160a1fb8b7da350e22013b619122"
    # Upstream deliberately ignores 127/8 resolvers to prevent routing loops.
    # This address exists only in this container's isolated network namespace.
    subprocess.run(["ip", "address", "add", "192.0.2.53/32", "dev", "lo"], check=True)  # nosec B603 - Literal argv, isolated network namespace only.
    subprocess.run(["ip", "address", "add", "192.0.2.54/32", "dev", "lo"], check=True)  # nosec B603 - Literal argv, isolated network namespace only.
    observed = []
    servers = [serve_dns(53, "system/direct", observed), serve_dns(5354, "VPN", observed)]
    with tempfile.TemporaryDirectory(prefix="ft-dns-") as directory, endpoint() as certificate:
        root = Path(directory)
        executable = root / "client"
        executable.write_bytes(data)
        executable.chmod(0o700)
        cases = [("general", [], "general.example", "VPN"),
                 ("selective", ["selected.example"], "unlisted.example", "system/direct"),
                 ("selective", ["selected.example"], "selected.example", "VPN"),
                 ("general", ["bypass.example"], "bypass.example", "system/direct")]
        for index, (mode, exclusions, domain, expected) in enumerate(cases):
            config = root / "client.toml"
            config.write_text(f'vpn_mode = "{mode}"\nkillswitch_enabled = true\n'
                              f'exclusions = {json.dumps(exclusions)}\nexclusions_preresolve_enabled = false\n'
                              'dns_upstreams = ["192.0.2.54:5354"]\n'
                              '[endpoint]\nhostname = "vpn.invalid"\naddresses = ["127.0.0.1:29443"]\n'
                              'has_ipv6 = false\nusername = "test"\npassword = "probe-only"\n'
                              'upstream_protocol = "http2"\n'
                              f'certificate = """{certificate.read_text()}"""\n'
                              '[listener.socks]\naddress = "127.0.0.1:21080"\n')
            log_path = Path(f"dns-policy-{index}.log")
            with log_path.open("w") as log:
                process = subprocess.Popen([str(executable), "-c", str(config), "-l", "debug"],  # nosec B603 - Digest-verified private copy and fixed argv.
                                           stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 15
                    while True:
                        assert process.poll() is None, f"Client exited: {log_path.read_text()[-4000:]}"
                        try:
                            query_dns(domain)
                            break
                        except (OSError, AssertionError):
                            if time.monotonic() >= deadline:
                                raise AssertionError(log_path.read_text()[-5000:]) from None
                            time.sleep(0.2)
                    routes = {label for label, name, _ in observed if name == domain}
                    assert routes == {expected}, (mode, domain, routes, expected)
                    senders = {sender for _, name, sender in observed if name == domain}
                    expected_sender = "trusttunnel_endpoint" if expected == "VPN" else "client"
                    assert senders == {expected_sender}, (domain, senders, expected_sender)
                    print(f"PASS: {mode}, {domain} -> {expected}", flush=True)
                finally:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
        assert len(servers) == 2  # Keep both DNS sockets alive until all cases end.


if __name__ == "__main__":
    main()
