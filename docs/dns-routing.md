# DNS routing investigation

Tested on 2026-11-15 against the official TrustTunnelClient 1.1.12 executable
(the version pinned by FreeTunnel), with a real TrustTunnel 1.1.0 endpoint.
The client, endpoint and two DNS responders ran inside one Docker network
namespace with no external network. No host DNS, routes or firewall changed.

| Core mode and queried domain | Observed DNS destination |
| --- | --- |
| General, ordinary domain | Configured DNS through the endpoint |
| Selective, domain outside the inclusion list | System DNS directly |
| Selective, domain in the inclusion list | Configured DNS through the endpoint |
| General, explicitly bypassed domain | System DNS directly |

Each case received a real DNS answer, checked which responder saw the query and
matched the sending UDP socket to the client or endpoint process in `/proc`;
the endpoint and client logs were retained. The VPN DNS uses `192.0.2.54`, since
upstream can access a `127/8` DNS upstream directly. This exercises UDP DNS through the
SOCKS listener, which reaches the same core DNS handler as TUN. It is not a
complete desktop/Android test of OS resolver attribution or encrypted DNS.

The result agrees with upstream
[`DnsHandler::on_dns_request`](https://github.com/TrustTunnel/TrustTunnelClient/blob/067deb837ed994d6ae902e3bb40b69f6266af216/core/src/dns_handler.cpp#L921)
and its
[`test_dns_routing.cpp`](https://github.com/TrustTunnel/TrustTunnelClient/blob/067deb837ed994d6ae902e3bb40b69f6266af216/core/test/test_dns_routing.cpp).
The default decision uses the queried domain, not the identity of the app that
originally asked the OS resolver. FreeTunnel's application callback can identify
the process owning the intercepted socket; it cannot recover the initiating
application from a shared resolver's socket. Thus an app inclusion alone is not
a guarantee that its OS-managed DNS travels through VPN. This is a limitation
inferred from the application callback and the reproduced core behavior, not a
claim that every OS/application combination leaks DNS.

## Corrections to the upstream policy

On 2026-11-17, the same isolated probe reproduced a case-sensitive routing bug
in the official 1.1.12 client: in general mode, the exclusion `bypass.example`
sent `bypass.example` directly, but sent `ByPaSs.ExAmPlE` through the endpoint.
Both queries received answers, and the sender-process check distinguished the
client from the endpoint. This demonstrates a core defect; it does not prove
the cause of any particular query on a user's machine.

FreeTunnel's `08-canonical-domain-routing.patch` normalizes ASCII letter case
and a terminal root dot when matching domain rules. It covers imported rules,
manual rules, geo lists and names learned from responses; it does not rewrite
the DNS packet or the hostname displayed in connection logs.

The patch also prevents application force-bypass/force-VPN decisions from
skipping the DNS domain handler for intercepted TCP/UDP port 53 connections.
Each query follows the domain policy, even when one resolver socket carries
queries for both direct and VPN destinations. Explicit rejection is preserved.
A failed direct DNS request must not be retried through the VPN as a fallback.

`09-direct-dns-during-reconnect.patch` evaluates the domain policy before the
endpoint-disconnected guard. With the kill switch enabled, bypass-domain DNS
continues directly during reconnection; VPN-domain DNS remains blocked. Both
split modes and manual-over-geo priorities are covered, including the alternate
DNS exclusion route. Existing endpoint-bootstrap exceptions and the behavior
with the kill switch disabled are preserved.

Native regression tests cover case variants, wildcard boundaries, manual/geo
priority, both split modes, forced application routes and explicit rejection.
Linux build CI runs the domain-filter and relevant DNS-routing tests against
the patched core.

## Shared addresses and address-family availability

The network audit on 2026-11-18 reproduced another routing defect: the core
cached a hostname by destination address, port and application. A later flow
from the same browser to another hostname on that shared CDN address could
inherit the previous hostname's direct route before its own hostname was read.

`12-current-connection-hostname.patch` prevents a new connection from treating
that cached name as its identity. Scannable traffic waits for its own HTTP
hostname, TLS SNI or supported QUIC hostname before applying domain rules.
Explicit application routing and higher-priority IP rules retain their existing
precedence. If no hostname can be extracted, IP/default rules apply; an older
flow's cached name is not a fallback. This does not inspect encrypted DNS or
separate multiple HTTP/2 origins multiplexed inside one encrypted connection.

The same tests reproduced premature rejection when the selected route supported
IPv6 but the other route did not. Classification now waits if either route can
carry the address family, then checks the selected route before opening it.
An unavailable selected route fails closed; it is never replaced with the other
route. The native matrix covers IPv4/IPv6, both split modes, plain and priority
rules, direct/VPN destinations, opaque traffic and asymmetric route availability.
During endpoint recovery, local hostname inspection remains available in both
split modes: direct sites can connect, while VPN sites fail closed before any
payload is forwarded.
The patch is included in both desktop builds and the stable Android adapter.

## Decision

Keep domain-based DNS routing and the existing Russian-sites bypass behavior. Do not
port Netlane's global DNS override as a default: it would also tunnel DNS for
bypassed destinations and could break local names. There is no general DNS
failure in the original lower-case full-tunnel or domain-based selective cases;
the corrections above address additional cases those tests did not cover.

A separate opt-in policy could address shared-resolver application routing, but
it needs a real OS/application reproducer and explicit local-name behavior
before adding another core option. For now, include the needed domains in
Through VPN as well, or use full-tunnel mode when all ordinary DNS must be
tunnelled. This policy governs intercepted ordinary DNS. An Android app excluded
from the VPN by the OS does not reach this handler. Browser/application DoH and
DoT follow their encrypted connection's routing; the queried names cannot be
read by this handler.

On desktop, system DNS may still enter the local TUN interface before this
decision. Entering TUN does not mean sending the query to the VPN endpoint.
Removing the interception globally would also prevent domain-based routing and
DNS-derived destination matching. Direct-domain queries leave through the
system DNS path; VPN-domain queries use the VPN DNS path.

## Reproduce

Use `tests/dns_policy_probe.py` only in a disposable container. It refuses to run
without `/.dockerenv`, root and `FT_DNS_CONTAINER_TEST=1`. It pins the client
executable SHA-256; `tests/incoming_endpoint.py` independently pins the endpoint.

Prepare the official archives, verify their SHA-256 before extraction:

- Client: `trusttunnel_client-v1.1.12-linux-x86_64.tar.gz`, SHA-256
  `75b7df59351e26462487464de5f3462153e00195f2d40677872fb93a5b44c488`.
- Endpoint: `trusttunnel-v1.1.0-linux-x86_64.tar.gz`, SHA-256
  `91c2ea3db7416a01b5258a4c047ec22890490bc55e1b194206031aa75144f0e7`.

Mount their extracted directories read-only at `/client` and `/endpoint`, the
repository's `tests` directory at `/src`, and a temporary log directory at
`/logs`. Build the image from `tests/installed-helper/Dockerfile`, then run with:

```sh
docker run --rm --network none --cap-add NET_ADMIN --dns 192.0.2.53 \
  -e FT_DNS_CONTAINER_TEST=1 \
  -e DNS_CLIENT_BIN=/client/trusttunnel_client \
  -e INCOMING_ENDPOINT_BIN=/endpoint/trusttunnel_endpoint \
  -v "$CLIENT_DIR:/client:ro" -v "$ENDPOINT_DIR:/endpoint:ro" \
  -v "$PWD/tests:/src:ro" -v "$LOG_DIR:/logs" -w /logs \
  ft-helper-tests python3 /src/dns_policy_probe.py
```

`NET_ADMIN` is used only to add `192.0.2.53/32` and `192.0.2.54/32` to this
container's loopback: upstream treats `127/8` DNS addresses specially. Never use
host networking for this probe. No TUN device or privileged container is needed.
