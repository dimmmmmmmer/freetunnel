# TrustTunnel 1.3 client random auth-key smoke

An isolated smoke passed on 2026-11-24 between the official stable
TrustTunnelClient 1.3.0 console client and TrustTunnel 1.3.1 endpoint. These
match the versions used by this release. The endpoint and SOCKS listener were
bound to loopback. Temporary credentials, an auth-key rule and a one-day
self-signed certificate were generated for the run. No production endpoint,
credentials, TUN device, route change, firewall rule or privileged container
was used.

## Verified release artifacts

| Component | Release commit | Linux x86_64 asset | SHA-256 |
| --- | --- | --- | --- |
| TrustTunnelClient | `232f151ea9864849a422666f1ce73ddc1bb4de3a` (`v1.3.0`) | `trusttunnel_client-v1.3.0-linux-x86_64.tar.gz` | `99e764bb2c080f7a60c441b51ad75fa7ec00e6c8a80afe36030af37201ba32f7` |
| TrustTunnel endpoint | `113cdc89559b9d2698e1477b3d0af89e95c0b7cf` (`v1.3.1`) | `trusttunnel-v1.3.1-linux-x86_64.tar.gz` | `29fd330eafab37eb8ab668dcbd57aa8f99b46e26b268e5dc9b416a483fadca97` |

The release assets were downloaded from the official GitHub repositories
`TrustTunnel/TrustTunnelClient` and `TrustTunnel/TrustTunnel`; each local
SHA-256 matched the release API's published `digest` field. Reproduce the
asset download and digest check with:

```sh
mkdir -p /tmp/ft-auth-smoke/bin
cd /tmp/ft-auth-smoke
GH_PAGER=cat gh release download v1.3.0 --repo TrustTunnel/TrustTunnelClient \
  --pattern 'trusttunnel_client-v1.3.0-linux-x86_64.tar.gz'
GH_PAGER=cat gh release download v1.3.1 --repo TrustTunnel/TrustTunnel \
  --pattern 'trusttunnel-v1.3.1-linux-x86_64.tar.gz'
echo '99e764bb2c080f7a60c441b51ad75fa7ec00e6c8a80afe36030af37201ba32f7  trusttunnel_client-v1.3.0-linux-x86_64.tar.gz' | sha256sum -c -
echo '29fd330eafab37eb8ab668dcbd57aa8f99b46e26b268e5dc9b416a483fadca97  trusttunnel-v1.3.1-linux-x86_64.tar.gz' | sha256sum -c -
tar -xzf trusttunnel_client-v1.3.0-linux-x86_64.tar.gz -C bin --strip-components=1
tar -xzf trusttunnel-v1.3.1-linux-x86_64.tar.gz -C bin --strip-components=1
```

The endpoint CLI supports `--client-random-auth-key`, and the client supports
a SOCKS5 listener at `[listener.socks]`. The harness requires these exact binary
versions. The client used `http2`, `tls_profile = "boringssl"`, and skipped
verification only for its temporary local certificate.

## Test method and result

The repeatable harness is [`scripts/check-core-auth-key.sh`](../scripts/check-core-auth-key.sh). It takes the verified binaries from `BIN_DIR` and creates all configuration and
fixtures in a fresh temporary directory, and runs the endpoint on loopback with an allow rule for
one temporary key followed by a deny rule. The client exposes a loopback SOCKS
port. A local HTTP fixture is addressed through a host-assigned non-loopback
address because the core deliberately sends SOCKS requests to loopback directly
instead of through the tunnel.

Choose the fixture address deliberately. It must be assigned to this host and
`ip route get ADDRESS` must report kernel local delivery (`local ADDRESS dev lo
table local`). The harness requires `FIXTURE_IP`, validates it as IPv4, and checks this route
before starting. The fixture binds to that address for the short test duration,
so choose a suitable local address and run only on a trusted host network.

```sh
export BIN_DIR=/path/to/verified-binaries
read -rp 'Host-assigned local IPv4 address: ' FIXTURE_IP
export FIXTURE_IP
scripts/check-core-auth-key.sh
```

The recorded run used a host-assigned address whose route lookup returned
`local ADDRESS dev lo table local`. Results:

- Correct key: `local-auth-ok` returned through the local SOCKS listener.
- Wrong key: the HTTP fixture was not reached.
- Missing key: the HTTP fixture was not reached.
- Endpoint logs confirmed an authenticated tunnel to the fixture and auth-key
  validation denials for both negative cases. The fixture recorded exactly one
  GET, from the correct-key request.

No TUN was created and no routes or firewall rules were changed. The harness
requires an explicit `FIXTURE_IP`, verifies the local-delivery route first, and
passed again after these checks were added.

## Scope

This is an upstream core/client-to-endpoint protocol check. It does not exercise
FreeTunnel's Qt wrapper, credential-store retrieval, config parsing, UI, or
packaging. The endpoint binary is from official TrustTunnel 1.3.1; the
client is from TrustTunnelClient 1.3.0. The desktop tests separately cover
credential retrieval, refusing export when an auth key cannot be read, and
preserving the key and TLS profile in shared configurations. This smoke alone
does not establish a connection through the FreeTunnel GUI.
