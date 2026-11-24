"""Connect from a clean installation of the real desktop client on a disposable host."""
import asyncio
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import signal
import socket
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from incoming_endpoint import endpoint

DEST = ("192.0.2.77", 42857)
MAC = sys.platform == "darwin"


async def serve_probe(reader, writer, key):
    # Only the endpoint's SOCKS forwarding can reach this loopback responder.
    # It accepts one destination and returns a proof for the caller's nonce.
    try:
        async with asyncio.timeout(10):
            version, count = await reader.readexactly(2)
            assert version == 5 and 0 in await reader.readexactly(count)
            writer.write(b"\x05\x00")
            assert await reader.readexactly(4) == b"\x05\x01\x00\x01"
            assert socket.inet_ntoa(await reader.readexactly(4)) == DEST[0]
            assert int.from_bytes(await reader.readexactly(2), "big") == DEST[1]
            writer.write(b"\x05\x00\x00\x01" + b"\0" * 6)
            nonce = await reader.readexactly(32)
            writer.write(hmac.digest(key, nonce, hashlib.sha256))
            await writer.drain()
    finally:
        writer.close()
        await writer.wait_closed()


def seed_profile(cert, address, password):
    base = Path.home() / ("Library/Preferences" if MAC else ".config") / "FreeTunnel/FreeTunnel"
    base.mkdir(parents=True, exist_ok=True)
    index = base / "configs.json"
    assert not index.exists(), "Desktop probe requires an empty profile"
    profile = base / "probe.toml"
    profile.touch(mode=0o600, exist_ok=False)
    profile.write_text('vpn_mode = "general"\nkillswitch_enabled = false\n'
                       f'[endpoint]\nhostname = "vpn.invalid"\naddresses = ["{address}:29443"]\n'
                       f'username = "test"\npassword = "{password}"\nupstream_protocol = "http2"\nhas_ipv6 = false\n'
                       f'certificate = """{cert.read_text()}"""\n'
                       '[listener.tun]\nchange_system_dns = false\nmtu_size = 1500\n'
                       f'included_routes = ["{DEST[0]}/32"]\nexcluded_routes = []\n')
    index.write_text(json.dumps([str(profile)]))


async def interface():
    args = ("/sbin/route", "-n", "get", DEST[0]) if MAC else ("ip", "-j", "route", "get", DEST[0])
    process = await asyncio.create_subprocess_exec(*args, stdout=asyncio.subprocess.PIPE)
    output, _ = await process.communicate()
    assert process.returncode == 0
    if MAC:
        return next(line.split()[-1] for line in output.decode().splitlines() if line.strip().startswith("interface:"))
    return json.loads(output)[0]["dev"]


async def desktop_session(exe, key, direct):
    process = await asyncio.create_subprocess_exec(str(exe), "freetunnel://connect", start_new_session=True)
    try:
        async with asyncio.timeout(150):
            while (tunnel := await interface()) == direct:
                assert process.returncode is None, f"Desktop exited with {process.returncode}"
                await asyncio.sleep(0.25)
        print(f"Route to {DEST[0]} uses {tunnel} (previously {direct})", flush=True)
        async with asyncio.timeout(20):
            reader, writer = await asyncio.open_connection(*DEST)
            try:
                nonce = secrets.token_bytes(32)
                writer.write(nonce)
                assert hmac.compare_digest(await reader.readexactly(32), hmac.digest(key, nonce, hashlib.sha256))
            finally:
                writer.close()
                await writer.wait_closed()
        disconnect = await asyncio.create_subprocess_exec(str(exe), "freetunnel://disconnect")
        assert await asyncio.wait_for(disconnect.wait(), 20) == 0
        async with asyncio.timeout(30):
            while await interface() != direct:
                await asyncio.sleep(0.25)
    finally:
        if process.returncode is None:
            os.killpg(process.pid, signal.SIGTERM)
            await asyncio.wait_for(process.wait(), 15)


async def main(exe):
    disposable = (os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted" or
                  (Path("/.dockerenv").is_file() and os.environ.get("FT_PORTABLE_CONTAINER_TEST") == "1"))
    if os.getuid() == 0 or not disposable:
        raise SystemExit("Run as an ordinary user on a disposable runner or test container")
    installed = Path("/Library/PrivilegedHelperTools/org.freetunnel.helper" if MAC else "/opt/freetunnel-helper")
    assert not installed.exists(), "First connection requires a clean machine"
    assert all(info[4][0] in ("127.0.0.1", "::1") for info in socket.getaddrinfo("github.com", 443))
    # The production client binds endpoint sockets to the physical uplink.
    # This UDP connect selects its own source address without sending a packet.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as uplink:
        uplink.connect(("192.0.2.1", 9))
        address = uplink.getsockname()[0]
    password = secrets.token_hex(24)
    key = secrets.token_bytes(32)
    direct = await interface()
    server = await asyncio.start_server(lambda r, w: serve_probe(r, w, key), "127.0.0.1", 0)
    async with server:
        with endpoint(server.sockets[0].getsockname()[1], "portable-endpoint.log", address, password) as cert:
            seed_profile(cert, address, password)
            await desktop_session(exe, key, direct)
            active = installed / "current"
            assert (active / "runtime").is_dir()
            stamp = active.lstat().st_mtime_ns
            print("PASS first desktop connection and authenticated TCP round trip", flush=True)
            await desktop_session(exe, key, direct)
            assert active.lstat().st_mtime_ns == stamp, "Restarting FreeTunnel reinstalled the helper"
            print("PASS desktop restart and authenticated TCP round trip with the same helper", flush=True)


if __name__ == "__main__":
    asyncio.run(main(Path(sys.argv[1]).resolve(strict=True)))
