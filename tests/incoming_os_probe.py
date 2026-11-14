#!/usr/bin/env python3
"""
Probe real TCP/UDP reply routing with two disposable OS tunnel interfaces.

The first interface injects packets from a synthetic remote peer. The second
has a more specific route to that peer, like a VPN overriding the uplink's
default route. Only documentation/benchmark addresses are routed. No default
route, DNS configuration, or existing firewall rule is changed.

This is an OS-level diagnostic, not an end-to-end VPN transport test. Run as
administrator on a disposable Windows/macOS runner. JSON records which
interface actually carried each response; baseline failure aborts the probe.
"""
import argparse
import ctypes
import ipaddress
import json
import os
from pathlib import Path
import socket
import struct
import subprocess  # nosec B404 - Fixed test tools and argument arrays on disposable OS runners.
import sys
import threading
import time


def run(*args):
    result = subprocess.run(args, check=True, capture_output=True, text=True)  # nosec B603 - Only test-owned commands/addresses.
    return result.stdout.strip()


def ps(command):
    return run("powershell", "-NoProfile", "-NonInteractive", "-Command",
               "$ErrorActionPreference = 'Stop'; " + command)


class MacTun:
    def __init__(self, label, v4, v6):
        """Create a test-owned utun interface with the supplied fixed addresses."""
        import fcntl
        self.sock = socket.socket(32, socket.SOCK_DGRAM, 2)
        info = fcntl.ioctl(self.sock, 0xc0644e03,
                           struct.pack("I96s", 0, b"com.apple.net.utun_control"))
        ctl_id = struct.unpack_from("I", info)[0]
        address = struct.pack("BBHII5I", 32, 32, 2, ctl_id, 0, 0, 0, 0, 0, 0)
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.connect(self.sock.fileno(), address, len(address)) != 0:
            raise OSError(ctypes.get_errno(), "connect utun")
        self.name = self.sock.getsockopt(2, 2, 64).split(b"\0")[0].decode()
        self.sock.setblocking(False)
        run("ifconfig", self.name, "inet", v4, v4.rsplit(".", 1)[0] + ".254",
            "netmask", "255.255.255.255", "up")
        run("ifconfig", self.name, "inet6", v6, "prefixlen", "128", "up")

    def route(self, prefix, add):
        args = ["route", "-n", "add" if add else "delete"]
        if ":" in prefix:
            args.append("-inet6")
        run(*args, "-net", prefix, "-interface", self.name)
        # A physical macOS uplink has an interface-scoped default route as well
        # as the global one. A socket with a fixed source uses that scoped
        # route even after the VPN replaces the global default. Include the
        # corresponding scoped test-prefix route in this synthetic uplink.
        if prefix.endswith(("/16", "/64")):
            run(*args, "-net", prefix, "-interface", self.name, "-ifscope", self.name)

    def inject(self, packet):
        family = socket.AF_INET6 if packet[0] >> 4 == 6 else socket.AF_INET
        self.sock.send(struct.pack("!I", family) + packet)

    def receive(self):
        try:
            return self.sock.recv(65540)[4:]
        except BlockingIOError:
            return None

    def close(self):
        self.sock.close()


class WinTun:
    dll = None

    def __init__(self, label, v4, v6):
        """Create a test-owned Wintun adapter and narrowly scoped firewall rule."""
        if WinTun.dll is None:
            dll = ctypes.WinDLL(str(Path(os.environ["WINTUN_DLL"]).resolve()), use_last_error=True)
            signatures = {
                "WintunCreateAdapter": (ctypes.c_void_p, [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_void_p]),
                "WintunCloseAdapter": (None, [ctypes.c_void_p]),
                "WintunGetAdapterLUID": (None, [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64)]),
                "WintunStartSession": (ctypes.c_void_p, [ctypes.c_void_p, ctypes.c_uint32]),
                "WintunEndSession": (None, [ctypes.c_void_p]),
                "WintunReceivePacket": (ctypes.c_void_p, [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]),
                "WintunReleaseReceivePacket": (None, [ctypes.c_void_p, ctypes.c_void_p]),
                "WintunAllocateSendPacket": (ctypes.c_void_p, [ctypes.c_void_p, ctypes.c_uint32]),
                "WintunSendPacket": (None, [ctypes.c_void_p, ctypes.c_void_p]),
            }
            for name, (ret, args) in signatures.items():
                fn = getattr(dll, name)
                fn.restype, fn.argtypes = ret, args
            WinTun.dll = dll
        self.name = f"FTIncoming-{label}-{os.getpid()}"
        self.adapter = self.dll.WintunCreateAdapter(self.name, "FT incoming test", None)
        if not self.adapter:
            raise ctypes.WinError(ctypes.get_last_error())
        luid, index = ctypes.c_uint64(), ctypes.c_uint32()
        self.dll.WintunGetAdapterLUID(self.adapter, ctypes.byref(luid))
        error = ctypes.WinDLL("iphlpapi").ConvertInterfaceLuidToIndex(ctypes.byref(luid), ctypes.byref(index))
        if error:
            raise ctypes.WinError(error)
        self.index = index.value
        self.session = self.dll.WintunStartSession(self.adapter, 0x400000)
        if not self.session:
            raise ctypes.WinError(ctypes.get_last_error())
        ps(f"Set-NetIPInterface -InterfaceIndex {self.index} -AddressFamily IPv4 -Dhcp Disabled; "
           f"New-NetIPAddress -InterfaceIndex {self.index} -IPAddress {v4} -PrefixLength 32 | Out-Null; "
           f"New-NetIPAddress -InterfaceIndex {self.index} -IPAddress {v6} -PrefixLength 128 | Out-Null; "
           f"Set-NetIPInterface -InterfaceIndex {self.index} -AutomaticMetric Disabled -InterfaceMetric 10; "
           f"New-NetFirewallRule -Name '{self.name}' -DisplayName '{self.name}' "
           "-Direction Inbound -Action Allow -Protocol Any -LocalAddress 198.18.0.1,2001:db8:10::1 "
           "-RemoteAddress 198.19.0.2,2001:db8:11::2 -Profile Any | Out-Null")

    def route(self, prefix, add):
        if add:
            next_hop = "::" if ":" in prefix else "0.0.0.0"  # nosec B104 - On-link route gateway, not a listening address.
            ps(f"New-NetRoute -InterfaceIndex {self.index} -DestinationPrefix '{prefix}' "
               f"-NextHop '{next_hop}' -RouteMetric 0 -PolicyStore ActiveStore | Out-Null")
        else:
            ps(f"Remove-NetRoute -InterfaceIndex {self.index} -DestinationPrefix '{prefix}' -Confirm:$false")

    def inject(self, packet):
        pointer = self.dll.WintunAllocateSendPacket(self.session, len(packet))
        if not pointer:
            raise ctypes.WinError(ctypes.get_last_error())
        ctypes.memmove(pointer, packet, len(packet))
        self.dll.WintunSendPacket(self.session, pointer)

    def receive(self):
        size = ctypes.c_uint32()
        pointer = self.dll.WintunReceivePacket(self.session, ctypes.byref(size))
        if not pointer:
            error = ctypes.get_last_error()
            if error == 259:  # ERROR_NO_MORE_ITEMS
                return None
            raise ctypes.WinError(error)
        packet = ctypes.string_at(pointer, size.value)
        self.dll.WintunReleaseReceivePacket(self.session, pointer)
        return packet

    def close(self):
        try:
            ps(f"Remove-NetFirewallRule -Name '{self.name}'")
        finally:
            self.dll.WintunEndSession(self.session)
            self.dll.WintunCloseAdapter(self.adapter)


def checksum(data):
    data += b"\0" * (len(data) % 2)
    value = sum(struct.unpack("!" + "H" * (len(data) // 2), data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def packet(src, dst, sport, dport, udp, payload=b"", seq=1000, ack=0, flags=2):
    source, target = ipaddress.ip_address(src), ipaddress.ip_address(dst)
    proto = 17 if udp else 6
    if udp:
        segment = struct.pack("!HHHH", sport, dport, 8 + len(payload), 0) + payload
        offset = 6
    else:
        segment = struct.pack("!HHIIBBHHH", sport, dport, seq, ack, 5 << 4, flags, 65535, 0, 0) + payload
        offset = 16
    if source.version == 4:
        pseudo = source.packed + target.packed + struct.pack("!BBH", 0, proto, len(segment))
    else:
        pseudo = source.packed + target.packed + struct.pack("!I3xB", len(segment), proto)
    segment = segment[:offset] + struct.pack("!H", checksum(pseudo + segment) or 65535) + segment[offset + 2:]
    if source.version == 4:
        header = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(segment), 0, 0, 64, proto, 0, source.packed, target.packed)
        header = header[:10] + struct.pack("!H", checksum(header)) + header[12:]
    else:
        header = struct.pack("!IHBB16s16s", 6 << 28, len(segment), proto, 64, source.packed, target.packed)
    return header + segment


def decode(data):
    if not data:
        return None
    version = data[0] >> 4
    if version == 4 and len(data) >= 20:
        offset, proto, source, target = (data[0] & 15) * 4, data[9], data[12:16], data[16:20]
    elif version == 6 and len(data) >= 40:
        offset, proto, source, target = 40, data[6], data[8:24], data[24:40]
    else:
        return None
    segment = data[offset:]
    if proto not in (6, 17) or len(segment) < (20 if proto == 6 else 8):
        return None
    sport, dport = struct.unpack_from("!HH", segment)
    return {"source": str(ipaddress.ip_address(source)), "target": str(ipaddress.ip_address(target)),
            "sport": sport, "dport": dport, "proto": proto,
            "seq": struct.unpack_from("!I", segment, 4)[0] if proto == 6 else 0,
            "flags": segment[13] if proto == 6 else 0,
            "payload": segment[(segment[12] >> 4) * 4 if proto == 6 else 8:]}


def receive(tunnels, predicate, timeout=2):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for label, tun in tunnels:
            # Ignore unrelated neighbor-discovery and control packets.
            for _ in range(100):
                raw = tun.receive()
                if raw is None:
                    break
                decoded = decode(raw)
                if decoded and predicate(decoded):
                    return label, decoded
        time.sleep(0.005)
    return "missing", None


def exercise(tunnels, family, udp, wildcard, port):
    local, remote = (("198.18.0.1", "198.19.0.2") if family == socket.AF_INET
                     else ("2001:db8:10::1", "2001:db8:11::2"))
    wildcard_address = "0.0.0.0" if family == socket.AF_INET else "::"  # nosec B104 - Wildcard listening is the case under test on disposable runners.
    server = socket.socket(family, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM)
    server.settimeout(3)
    if family == socket.AF_INET6:
        server.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
    server.bind((wildcard_address if wildcard else local, port))
    if not udp:
        server.listen(1)
    failures = []

    def echo():
        try:
            if udp:
                data, addr = server.recvfrom(128)
                server.sendto(data, addr)
            else:
                connection, _ = server.accept()
                with connection:
                    connection.settimeout(3)
                    connection.sendall(connection.recv(128))
        except OSError as error:
            failures.append(str(error))

    worker = threading.Thread(target=echo, daemon=True)
    worker.start()
    peer_port = port + 1000
    payload = b"freetunnel-incoming-probe"
    tunnels[0][1].inject(packet(remote, local, peer_port, port, udp, payload if udp else b""))
    predicate = lambda p: p["dport"] == peer_port and p["sport"] == port and p["proto"] == (17 if udp else 6)
    route, reply = receive(tunnels, predicate)
    if not udp and reply and reply["flags"] & 0x12 == 0x12:
        ack = reply["seq"] + 1
        tunnels[0][1].inject(packet(remote, local, peer_port, port, False, seq=1001, ack=ack, flags=16))
        tunnels[0][1].inject(packet(remote, local, peer_port, port, False, payload, seq=1001, ack=ack, flags=24))
        route, reply = receive(tunnels, lambda p: predicate(p) and p["payload"] == payload)
        tunnels[0][1].inject(packet(remote, local, peer_port, port, False, seq=1001 + len(payload), ack=ack, flags=20))
    worker.join(3.5)
    server.close()
    return {"family": 4 if family == socket.AF_INET else 6, "protocol": "UDP" if udp else "TCP",
            "binding": "wildcard" if wildcard else "local-address", "route": route,
            "source": reply["source"] if reply else None,
            "echo": bool(reply and reply["payload"] == payload), "server_errors": failures}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="incoming-os-results.json")
    args = parser.parse_args()
    if sys.platform not in ("darwin", "win32"):
        parser.error("Run on Windows or macOS; Linux has test_linux_reply_routing.py")
    implementation = MacTun if sys.platform == "darwin" else WinTun
    tunnels, routes, results = [], [], []
    try:
        for label, v4, v6 in [("uplink", "198.18.0.1", "2001:db8:10::1"),
                               ("vpn", "198.18.1.1", "2001:db8:12::1")]:
            tunnels.append((label, implementation(label, v4, v6)))
        for prefix in ("198.19.0.0/16", "2001:db8:11::/64"):
            tunnels[0][1].route(prefix, True)
            routes.append((tunnels[0][1], prefix))
        time.sleep(3)  # IPv6 duplicate-address detection.
        port = 18080
        for active in (False, True):
            if active:
                for prefix in ("198.19.0.2/32", "2001:db8:11::2/128"):
                    tunnels[1][1].route(prefix, True)
                    routes.append((tunnels[1][1], prefix))
            for family in (socket.AF_INET, socket.AF_INET6):
                for udp in (False, True):
                    for wildcard in (False, True):
                        port += 1
                        result = {"vpn_route": active, **exercise(tunnels, family, udp, wildcard, port)}
                        results.append(result)
                        print(json.dumps(result), flush=True)
                        if not active and (result["route"] != "uplink" or not result["echo"]):
                            raise AssertionError("Baseline cannot deliver real echo traffic")
                        if active and not (udp and wildcard):
                            assert result["route"] == "uplink" and result["echo"], result
            if active:
                for family, remote in [(socket.AF_INET, "198.19.0.2"), (socket.AF_INET6, "2001:db8:11::2")]:
                    with socket.socket(family, socket.SOCK_DGRAM) as outgoing:
                        outgoing.sendto(b"new-outgoing", (remote, 19999))
                        route, _ = receive(tunnels, lambda p: p["dport"] == 19999 and p["payload"] == b"new-outgoing")
                        print(f"IPv{4 if family == socket.AF_INET else 6} outgoing control: {route}", flush=True)
                        assert route == "vpn", "VPN route did not capture new outgoing traffic"
    finally:
        Path(args.output).write_text(json.dumps(results, indent=2) + "\n")
        for tun, prefix in reversed(routes):
            try:
                tun.route(prefix, False)
            except Exception as error:
                print(f"route cleanup: {error}", file=sys.stderr)
        for _, tun in reversed(tunnels):
            tun.close()


if __name__ == "__main__":
    main()
