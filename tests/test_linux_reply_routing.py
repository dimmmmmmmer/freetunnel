#!/usr/bin/env python3
"""Exercise the shipped nft rules with real sockets in disposable namespaces."""
import ctypes
import os
from pathlib import Path
import re
import socket
import subprocess  # nosec B404 - fixed argv in disposable network namespaces, never a shell
import sys
import threading


def run(*args):
    return subprocess.run(args, check=True, text=True, capture_output=True).stdout  # nosec B603 - test-owned argv only


def exercise(peer_pid, ipv6, udp, expect_reply):
    family = socket.AF_INET6 if ipv6 else socket.AF_INET
    server_ip, peer_ip = ("2001:db8:1::1", "2001:db8:1::2") if ipv6 else ("192.0.2.1", "192.0.2.2")
    port = 18081 if udp else 18080
    server = socket.socket(family, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.settimeout(2)
    server.bind((server_ip, port))
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


def isolated():
    # The outer unshare creates a user namespace too: no host root or network
    # changes are needed. A second network namespace supplies the remote peer.
    ready_r, ready_w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(ready_r)
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.unshare(0x40000000) != 0:  # CLONE_NEWNET
            os._exit(3)
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
        for family in [[], ["-6"]]:
            run("ip", *family, "route", "add", "default", "dev", "tun0", "table", "880")
            run("ip", *family, "rule", "add", "prio", "30801", "lookup", "880")
        for v6 in [False, True]:
            for udp in [False, True]:
                exercise(pid, v6, udp, False)

        # Read the exact shipped rules, not a copy maintained only by the test.
        patch = (Path(__file__).resolve().parents[1] / "vendor/trusttunnel/05-linux-incoming-replies.patch").read_text()
        added = "\n".join(line[1:] for line in patch.splitlines() if line.startswith("+") and not line.startswith("+++"))
        rules = re.search(r'REPLY_RULESET = R"nft\((.*?)\)nft"', added, re.S).group(1)
        mark = re.search(r'REPLY_MARK = "([^"]+)"', added).group(1)
        run("nft", "add table inet unrelated")
        run("nft", rules)
        for family in [[], ["-6"]]:
            run("ip", *family, "rule", "add", "prio", "30799", "fwmark", mark, "lookup", "main")
        for v6 in [False, True]:
            for udp in [False, True]:
                exercise(pid, v6, udp, True)
        for family, address in [([], "192.0.2.2"), (["-6"], "2001:db8:1::2")]:
            route = run("ip", *family, "route", "get", address)
            assert "dev tun0" in route, route  # new outgoing connections stay intercepted
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
        isolated()
    else:
        raise SystemExit(subprocess.call(["unshare", "--user", "--map-root-user", "--net",  # nosec B603 - re-exec this test without a shell
                                         sys.executable, __file__, "--isolated"]))
