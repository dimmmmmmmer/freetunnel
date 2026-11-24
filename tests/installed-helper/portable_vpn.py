"""Exercise the shipped launcher and a real TUN session on a disposable runner."""
import asyncio
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import socket
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from incoming_endpoint import endpoint

DEST = ("192.0.2.77", 42857)
MAC = sys.platform == "darwin"


async def probe_session(exe, cert, token_path, first, seen, nonce, address, password):
    token = secrets.token_hex(16)
    token_path.write_text(token)
    token_path.chmod(0o600)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    # The application accepts only high ports for its local IPC.
    if port < 49152:
        with socket.socket() as probe:
            for port in range(55000, 55100):
                try:
                    probe.bind(("127.0.0.1", port))
                    break
                except OSError:
                    continue
            else:
                raise AssertionError("No free helper port")
    process = await asyncio.create_subprocess_exec(
        str(exe), "--launch-installed-helper", "--port", str(port), "--token-file", str(token_path),
        stdout=asyncio.subprocess.PIPE)
    output = asyncio.create_task(process.stdout.read())
    writer = None
    try:
        async with asyncio.timeout(150):
            while True:
                assert process.returncode is None, f"Launcher exited with {process.returncode}"
                try:
                    reader, writer = await asyncio.open_connection("127.0.0.1", port)
                    break
                except OSError:
                    await asyncio.sleep(0.2)

        def send(**message):
            writer.write(json.dumps(message).encode() + b"\n")

        async def event():
            line = await asyncio.wait_for(reader.readline(), 30)
            assert line, "Helper closed IPC"
            return json.loads(line)

        def proof(role, challenge):
            return hmac.new(token.encode(), f"{role}:{challenge}".encode(), hashlib.sha256).hexdigest()

        challenge = secrets.token_hex(16)
        send(cmd="hello", nonce=challenge)
        reply = await event()
        assert reply["ev"] == "challenge" and hmac.compare_digest(reply["proof"], proof("helper", challenge))
        send(cmd="auth", proof=proof("gui", reply["nonce"]))
        assert (await event())["ev"] == "ready"
        # Only the documentation address enters the VPN. The endpoint forwards
        # it to our loopback-only responder; no host interface owns DEST.
        config = (f'vpn_mode = "general"\nkillswitch_enabled = false\n'
                  f'[endpoint]\nhostname = "vpn.invalid"\naddresses = ["{address}:29443"]\n'
                  f'username = "test"\npassword = "{password}"\nupstream_protocol = "http2"\nhas_ipv6 = false\n'
                  f'certificate = """{cert.read_text()}"""\n'
                  '[listener.tun]\nchange_system_dns = false\nmtu_size = 1500\n'
                  f'included_routes = ["{DEST[0]}/32"]\nexcluded_routes = []\n')
        send(cmd="connect", configToml=config, loggingEnabled=False)
        async with asyncio.timeout(45):
            while True:
                message = await event()
                if message.get("ev") == "error":
                    raise AssertionError(message.get("msg"))
                if message.get("ev") == "state":
                    print("VPN", message["state"], flush=True)
                    if message["state"] == "Connected":
                        break
        async with asyncio.timeout(15):
            traffic, stream = await asyncio.open_connection(*DEST)
            try:
                stream.write(nonce)
                assert await traffic.readexactly(32) == hashlib.sha256(nonce).digest()
                await seen.wait()
            finally:
                stream.close()
                await stream.wait_closed()
        send(cmd="disconnect")
        await writer.drain()
    finally:
        if writer:
            writer.close()
            await writer.wait_closed()
        try:
            await asyncio.wait_for(process.wait(), 12)
        except TimeoutError:
            process.terminate()
            await process.wait()
        phases = (await output).decode().splitlines()
        print("Launcher:", phases, flush=True)
    assert phases == (["preparing", "installing", "starting"] if first else ["starting"]), phases


async def main(exe):
    if os.getuid() == 0 or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise SystemExit("Run as the ordinary user on an isolated GitHub-hosted runner")
    installed = Path("/Library/PrivilegedHelperTools/org.freetunnel.helper" if MAC else "/opt/freetunnel-helper")
    assert not installed.exists(), "First connection requires a clean machine"
    # The old network bootstrap would fail here. The endpoint remains reachable.
    assert all(info[4][0] in ("127.0.0.1", "::1") for info in socket.getaddrinfo("github.com", 443))
    # The production client binds endpoint sockets to the physical uplink.
    # Use that interface's own IPv4 address. The UDP connect only selects a
    # source; it sends no packet. The endpoint allows exactly that source.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as uplink:
        uplink.connect(("192.0.2.1", 9))
        address = uplink.getsockname()[0]
    password = secrets.token_hex(24)
    for first in (True, False):
        nonce = secrets.token_bytes(32)
        seen = asyncio.Event()

        async def respond(reader, writer):
            try:
                async with asyncio.timeout(10):
                    version, count = await reader.readexactly(2)
                    assert version == 5 and 0 in await reader.readexactly(count)
                    writer.write(b"\x05\x00")
                    assert await reader.readexactly(4) == b"\x05\x01\x00\x01"
                    assert socket.inet_ntoa(await reader.readexactly(4)) == DEST[0]
                    assert int.from_bytes(await reader.readexactly(2), "big") == DEST[1]
                    writer.write(b"\x05\x00\x00\x01" + b"\0" * 6)
                    assert await reader.readexactly(len(nonce)) == nonce
                    writer.write(hashlib.sha256(nonce).digest())
                    await writer.drain()
                    seen.set()
            finally:
                writer.close()
                await writer.wait_closed()

        server = await asyncio.start_server(respond, "127.0.0.1", 0)
        async with server:
            with endpoint(server.sockets[0].getsockname()[1], "portable-endpoint.log", address, password) as cert:
                with tempfile.TemporaryDirectory() as temp:
                    await probe_session(exe, cert, Path(temp) / ".fthelper-probe", first, seen, nonce, address, password)
        assert (installed / "current/runtime").is_dir()
        print("PASS", "first connection" if first else "reconnection", "authenticated TCP round trip", flush=True)


if __name__ == "__main__":
    asyncio.run(main(Path(sys.argv[1]).resolve(strict=True)))
