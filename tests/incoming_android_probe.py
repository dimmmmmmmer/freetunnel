#!/usr/bin/env python3
"""External emulator peer: real incoming traffic to a separate application's UID."""
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess  # nosec B404 - Fixed adb test commands, argument arrays, no host shell.
import time

from incoming_endpoint import endpoint


def adb(*args):
    return subprocess.run(["adb", *args], check=True, text=True, capture_output=True).stdout.strip()  # nosec B603 - Test-owned adb commands and hex tokens only.


def wait_for(condition, seconds=45):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.25)
    raise AssertionError("Timed out waiting for the fixture")


def ready():
    try:
        return bool(adb("shell", "run-as", "org.freetunnel.incomingprobe", "cat", "files/ready"))
    except subprocess.CalledProcessError:
        return False


def outgoing_probe(receiver, package, active, bypass=False):
    token = secrets.token_hex(8)
    component = package + "/org.freetunnel.incomingprobe.MainActivity"
    adb("shell", "am", "start", "-W", "-n", component, "--es", "command", "outgoing", "--es", "token", token)
    data, peer = receiver.recvfrom(128)
    assert data == ("new-outgoing:" + token).encode(), (data, peer)
    # The endpoint routes its copy to a runner-local 10.0.2.2/32 address.
    # Emulator-direct traffic comes from 127.0.0.1, so both paths deliver real
    # data but remain distinguishable. Do not equate a timeout with VPN success.
    assert peer[0] in ("127.0.0.1", "10.0.2.2"), peer
    direct = peer[0] == "127.0.0.1"
    receiver.sendto(data, peer)

    def received_echo():
        try:
            return adb("shell", "run-as", package, "cat", "files/outgoing-" + token) == "ok"
        except subprocess.CalledProcessError:
            return False

    wait_for(received_echo, seconds=8)
    row = {"vpn_route": active, "server_app_bypassed": bypass,
           "kind": "new-outgoing", "application": package,
           "direct": direct, "echo": True, "peer": list(peer)}
    print(json.dumps(row), flush=True)
    expected_direct = not active or (bypass and package == "org.freetunnel.incomingprobe")
    assert direct == expected_direct, row
    return row


def probe(active, bypass=False):
    # Android 15 can freeze a cached background Activity and suspend its
    # networking. Keep the server visible while measuring incoming traffic;
    # only the brief outgoing control measurement uses the other Activity.
    adb("shell", "am", "start", "-W", "-n", "org.freetunnel.incomingprobe/.MainActivity")
    results = []
    for port, protocol, binding in [(18081, "tcp", "local-address"), (18082, "tcp", "wildcard"),
                                    (18083, "udp", "local-address"), (18084, "udp", "wildcard")]:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM if protocol == "tcp" else socket.SOCK_DGRAM) as peer:
            peer.settimeout(2)
            try:
                peer.connect(("127.0.0.1", port))
                peer.sendall(b"incoming-peer")
                ok, error = peer.recv(128) == b"incoming-peer", None
            except OSError as exc:
                ok, error = False, str(exc)
        row = {"vpn_route": active, "server_app_bypassed": bypass, "family": 4, "protocol": protocol.upper(), "binding": binding,
               "echo": ok, "error": error}
        print(json.dumps(row), flush=True)
        results.append(row)
    return results


def main():
    results = []
    process = None
    try:
        adb("shell", "rm", "-f", "/data/local/tmp/incoming-vpn-ready", "/data/local/tmp/incoming-vpn-stop")
        for protocol, port in [("tcp", 18081), ("tcp", 18082), ("udp", 18083), ("udp", 18084)]:
            adb("emu", "redir", "add", f"{protocol}:{port}:{port}")
        adb("shell", "am", "start", "-n", "org.freetunnel.incomingprobe/.MainActivity")
        wait_for(ready)
        print("Fixture uplink: " + adb("shell", "run-as", "org.freetunnel.incomingprobe", "cat", "files/ready"), flush=True)
        results += probe(False)
        assert all(row["echo"] for row in results), "Baseline incoming traffic failed"
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as outgoing:
            outgoing.bind(("0.0.0.0", 19999))  # nosec B104 - Receives both loopback aliases on a disposable runner; only synthetic probe data.
            outgoing.settimeout(5)
            for package in ("org.freetunnel.incomingprobe", "org.freetunnel.incomingprobe.control"):
                results.append(outgoing_probe(outgoing, package, False))
            adb("shell", "am", "start", "-W", "-n", "org.freetunnel.incomingprobe/.MainActivity")
            for bypass in (False, True):
                adb("shell", "rm", "-f", "/data/local/tmp/incoming-vpn-ready", "/data/local/tmp/incoming-vpn-stop")
                log_path = Path(f"incoming-android-instrumentation-{bypass}.txt")
                with log_path.open("w") as log:
                    process = subprocess.Popen(["adb", "shell", "am", "instrument", "-w", "-e", "class",  # nosec B603 - Fixed instrumentation command; only a boolean varies.
                        "org.freetunnel.android.IncomingRoutingDeviceTest", "-e", "incoming_probe", "true",
                        "-e", "bypass_probe_app", str(bypass).lower(),
                        "org.freetunnel.android.test/androidx.test.runner.AndroidJUnitRunner"], stdout=log, stderr=subprocess.STDOUT)
                    wait_for(lambda: adb("shell", "if test -f /data/local/tmp/incoming-vpn-ready; then echo ready; fi").strip() == "ready")
                    rows = probe(True, bypass)
                    results += rows
                    # NEW connections of this same server application must
                    # stay in the VPN. A separate UID alone is
                    # not sufficient to verify that requirement.
                    for package in ("org.freetunnel.incomingprobe", "org.freetunnel.incomingprobe.control"):
                        results.append(outgoing_probe(outgoing, package, True, bypass))
                    adb("shell", "echo stopped > /data/local/tmp/incoming-vpn-stop")
                    assert process.wait(timeout=45) == 0
                output = log_path.read_text()
                print(output)
                assert "OK (1 test)" in output and "FAILURES" not in output
                assert all(row["echo"] for row in rows if bypass or row["protocol"] == "TCP"), rows
    finally:
        Path("incoming-android-results.json").write_text(json.dumps(results, indent=2) + "\n")
        try:
            adb("shell", "echo stopped > /data/local/tmp/incoming-vpn-stop")
            if process and process.poll() is None:
                process.wait(timeout=45)
        finally:
            Path("incoming-android-logcat.txt").write_text(adb("logcat", "-d", "-v", "threadtime"))
            adb("shell", "am", "force-stop", "org.freetunnel.incomingprobe")
            adb("shell", "am", "force-stop", "org.freetunnel.incomingprobe.control")
    udp_ok = all(row["echo"] for row in results if row.get("protocol") == "UDP"
                 and row["vpn_route"] and not row["server_app_bypassed"])
    report = ("Incoming Android UDP while the server stays in VPN: "
              + ("PASS" if udp_ok else "UNRESOLVED — replies time out")
              + ". App-wide bypass is not a fix for this requirement.\n")
    print(report, flush=True)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
            summary.write(report)
    if os.environ.get("REQUIRE_ANDROID_UDP") == "true":
        assert udp_ok, report


if __name__ == "__main__":
    with endpoint() as certificate:
        adb("push", str(certificate), "/data/local/tmp/incoming-endpoint.pem")
        adb("reverse", "tcp:29443", "tcp:29443")
        try:
            main()
        finally:
            adb("reverse", "--remove", "tcp:29443")
            adb("shell", "rm", "-f", "/data/local/tmp/incoming-endpoint.pem")
