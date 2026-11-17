#!/usr/bin/env python3
"""Exercise the shipped nft rules with real sockets in disposable namespaces."""
import ctypes
import os
from pathlib import Path
import re
import shlex
import socket
import subprocess  # nosec B404 - fixed argv in disposable network namespaces, never a shell
import sys
import threading
import time


def run(*args):
    return subprocess.run(args, check=True, text=True, capture_output=True).stdout  # nosec B603 - test-owned argv only


def exercise(peer_pid, ipv6, udp, expect_reply, wildcard=False):
    family = socket.AF_INET6 if ipv6 else socket.AF_INET
    server_ip, peer_ip = ("2001:db8:1::1", "2001:db8:1::2") if ipv6 else ("192.0.2.1", "192.0.2.2")
    port = 18081 if udp else 18080
    server = socket.socket(family, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.settimeout(2)
    server.bind((("::" if ipv6 else "0.0.0.0") if wildcard else server_ip, port))  # nosec B104 - Wildcard test in an isolated network namespace.
    if not udp:
        server.listen()
    failures = []

    def echo():
        try:
            if udp:
                data, addr = server.recvfrom(128)
                server.sendto(data, addr)
            else:
                conn, _ = server.accept()
                with conn:
                    conn.settimeout(2)
                    conn.sendall(conn.recv(128))
        except TimeoutError:
            if expect_reply:
                failures.append("server timed out")
        except OSError as error:
            if expect_reply:
                failures.append(str(error))

    thread = threading.Thread(target=echo)
    thread.start()
    program = '''import socket, sys
s = socket.socket(int(sys.argv[1]), int(sys.argv[2]))
s.settimeout(0.6)
s.bind((sys.argv[4], 0))
try:
    s.connect((sys.argv[3], int(sys.argv[5])))
    s.sendall(b"routing-test")
    assert s.recv(128) == b"routing-test"
except TimeoutError:
    sys.exit(2)
'''
    result = subprocess.run(["nsenter", "--net=/proc/%d/ns/net" % peer_pid,  # nosec B603 - PID from fork, fixed client code
                             sys.executable, "-c", program, str(int(family)),
                             str(int(socket.SOCK_DGRAM if udp else socket.SOCK_STREAM)),
                             server_ip, peer_ip, str(port)], capture_output=True, text=True)
    thread.join(3)
    server.close()
    assert not thread.is_alive(), "server thread did not finish"
    assert not failures, failures
    assert result.returncode == (0 if expect_reply else 2), result.stderr
    print(f"{'IPv6' if ipv6 else 'IPv4'} {'UDP' if udp else 'TCP'}: "
          f"{'reply preserved' if expect_reply else 'interception reproduced'}", flush=True)


def outgoing_capture():
    # Inspect real packets after OUTPUT, not `ip route get`, which only sees the
    # initial source-selection route. Reuse the incoming service's local port
    # too: a reply exception must never become an exemption for the whole port.
    for family, address in [(socket.AF_INET, "192.0.2.2"), (socket.AF_INET6, "2001:db8:1::2")]:
        for local_port, bound_interface in [(0, None), (18081, None), (0, "uplink")]:
            expected_device = bound_interface or "tun0"
            payload = b"freetunnel-outgoing-capture-check"
            with socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3)) as capture:
                capture.bind((expected_device, 0))
                capture.settimeout(2)
                with socket.socket(family, socket.SOCK_DGRAM) as outgoing:
                    outgoing.setsockopt(socket.SOL_SOCKET, socket.SO_MARK, 0x01000000)
                    if bound_interface:
                        # The core protects its direct/endpoint sockets this way.
                        outgoing.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, bound_interface.encode() + b"\0")
                    outgoing.bind(("0.0.0.0" if family == socket.AF_INET else "::", local_port))  # nosec B104 - Isolated test network.
                    # Test both sendto and repeated sends on a connected socket;
                    # the latter can reuse a cached initial route.
                    for connected in (False, True, True):
                        if connected:
                            outgoing.connect((address, 20000))
                            outgoing.send(payload)
                        else:
                            outgoing.sendto(payload, (address, 20000))
                        deadline = time.monotonic() + 2
                        while time.monotonic() < deadline:
                            packet = capture.recv(4096)
                            if payload in packet:
                                break
                        else:
                            raise AssertionError(f"Outgoing UDP missing from {expected_device}")
            print(f"IPv{4 if family == socket.AF_INET else 6} UDP port={local_port} "
                  f"bound={bound_interface}: captured on {expected_device}", flush=True)


def isolated(wildcard=False):
    # The outer unshare creates a user namespace too: no host root or network
    # changes are needed. A second network namespace supplies the remote peer.
    ready_r, ready_w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(ready_r)
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.unshare(0x40000000) != 0:  # CLONE_NEWNET
            os._exit(3)
        # Keep a receiver open for direct core-style probes. Otherwise the peer
        # sends ICMP port-unreachable, making later connected sends fail even
        # though their routing is correct.
        sinks = []
        for family, address in [(socket.AF_INET, "0.0.0.0"), (socket.AF_INET6, "::")]:  # nosec B104 - Isolated test peer.
            sink = socket.socket(family, socket.SOCK_DGRAM)
            if family == socket.AF_INET6:
                sink.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
            sink.bind((address, 20000))
            sinks.append(sink)
        os.write(ready_w, b"ready")
        os.close(ready_w)
        threading.Event().wait(120)
        os._exit(0)
    os.close(ready_w)
    try:
        assert os.read(ready_r, 5) == b"ready"
        run("ip", "link", "set", "lo", "up")
        run("ip", "link", "add", "uplink", "type", "veth", "peer", "name", "peer")
        run("ip", "link", "set", "peer", "netns", str(pid))
        peer = ["nsenter", f"--net=/proc/{pid}/ns/net"]
        for prefix, dev, v4, v6 in [([], "uplink", "192.0.2.1/24", "2001:db8:1::1/64"),
                                  (peer, "peer", "192.0.2.2/24", "2001:db8:1::2/64")]:
            run(*prefix, "ip", "link", "set", "lo", "up")
            run(*prefix, "ip", "link", "set", dev, "up")
            run(*prefix, "ip", "addr", "add", v4, "dev", dev)
            run(*prefix, "ip", "-6", "addr", "add", v6, "dev", dev, "nodad")
        run("sysctl", "-qw", "net.ipv4.conf.all.rp_filter=0", "net.ipv4.conf.uplink.rp_filter=0")
        run("ip", "link", "add", "tun0", "type", "dummy")
        run("ip", "link", "set", "tun0", "up")
        # An addressless dummy hides the real source-address selection issue:
        # a wildcard UDP reply can select the VPN address before conntrack.
        run("ip", "addr", "add", "172.20.2.13/32", "dev", "tun0")
        run("ip", "-6", "addr", "add", "fdfd:29::2/128", "dev", "tun0", "nodad")
        for family in [[], ["-6"]]:
            run("ip", *family, "route", "add", "default", "dev", "tun0", "table", "880")
            run("ip", *family, "rule", "add", "prio", "30801", "lookup", "880")
        for v6 in [False, True]:
            for udp in [False, True]:
                exercise(pid, v6, udp, False, wildcard)

        # Read the exact shipped rules, not a copy maintained only by the test.
        patch = (Path(__file__).resolve().parents[1] / "vendor/trusttunnel/10-wildcard-udp-replies.patch").read_text()
        # This patch contains the complete resulting ruleset as context/additions.
        # Patch verification checks that the context matches the pinned core.
        resulting = "\n".join(line[1:] for line in patch.splitlines()
                              if line.startswith(("+", " ")) and not line.startswith("+++"))
        rules = re.search(r'REPLY_RULESET = R"nft\((.*?)\)nft"', resulting, re.S).group(1)
        mark = re.search(r'REPLY_MARK = "([^"]+)"', resulting).group(1)
        initial = shlex.split(re.search(r'UDP_INITIAL_RULE =\s*"([^"]+)"', resulting).group(1))
        run("nft", "add table inet unrelated")
        run("nft", rules)
        for family in [[], ["-6"]]:
            run("ip", *family, "rule", "add", "prio", "30799", "fwmark", mark, "lookup", "main")
            run("ip", *family, "rule", "add", *initial)
        for v6 in [False, True]:
            for udp in [False, True]:
                exercise(pid, v6, udp, True, wildcard)
        outgoing_capture()
        # NOTRACK must not turn into an accidental bypass of the capture rule.
        run("nft", "add chain inet unrelated raw { type filter hook output priority raw; policy accept; }")
        run("nft", "add rule inet unrelated raw udp dport 20000 notrack")
        outgoing_capture()
        for family in [[], ["-6"]]:
            run("ip", *family, "rule", "del", *initial)
            run("ip", *family, "rule", "del", "prio", "30799", "fwmark", mark, "lookup", "main")
        run("nft", "delete table inet freetunnel_reply")
        assert "unrelated" in run("nft", "list tables")
        assert "freetunnel_reply" not in run("nft", "list tables")
        print("Outgoing capture and unrelated firewall table preserved; cleanup passed", flush=True)
    finally:
        os.close(ready_r)
        os.kill(pid, 15)
        os.waitpid(pid, 0)


if __name__ == "__main__":
    if "--isolated" in sys.argv:
        isolated("--wildcard" in sys.argv)
    else:
        raise SystemExit(subprocess.call(["unshare", "--user", "--map-root-user", "--net",  # nosec B603 - re-exec this test without a shell
                                         sys.executable, __file__, "--isolated", *sys.argv[1:]]))
