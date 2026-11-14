#!/usr/bin/env python3
"""External emulator peer: real incoming traffic to a separate application's UID."""
import json
from pathlib import Path
import socket
import subprocess
import time


def adb(*args):
    return subprocess.run(["adb", *args], check=True, text=True, capture_output=True).stdout.strip()


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
            outgoing.bind(("0.0.0.0", 19999))
            outgoing.settimeout(3)
            command = ["shell", "am", "start", "-n", "org.freetunnel.incomingprobe.control/org.freetunnel.incomingprobe.MainActivity", "--es", "command", "outgoing"]
            adb(*command)
            assert outgoing.recv(128) == b"new-outgoing", "Outgoing baseline failed"
            adb("shell", "am", "start", "-W", "-n", "org.freetunnel.incomingprobe/.MainActivity")
            for bypass in (False, True):
                adb("shell", "rm", "-f", "/data/local/tmp/incoming-vpn-ready", "/data/local/tmp/incoming-vpn-stop")
                log_path = Path(f"incoming-android-instrumentation-{bypass}.txt")
                with log_path.open("w") as log:
                    process = subprocess.Popen(["adb", "shell", "am", "instrument", "-w", "-e", "class",
                        "org.freetunnel.android.IncomingRoutingDeviceTest", "-e", "incoming_probe", "true",
                        "-e", "bypass_probe_app", str(bypass).lower(),
                        "org.freetunnel.android.test/androidx.test.runner.AndroidJUnitRunner"], stdout=log, stderr=subprocess.STDOUT)
                    wait_for(lambda: adb("shell", "if test -f /data/local/tmp/incoming-vpn-ready; then echo ready; fi").strip() == "ready")
                    rows = probe(True, bypass)
                    results += rows
                    adb(*command)
                    try:
                        data = outgoing.recv(128)
                        raise AssertionError(f"The included control app bypassed the VPN: {data!r}")
                    except socket.timeout:
                        print("New outgoing traffic from the control app is captured by the VPN", flush=True)
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


if __name__ == "__main__":
    main()
