# Incoming reply routing investigation

The requirement is to keep replies to incoming services on the original
network while **new connections from the same application remain in the
VPN**. Excluding the whole application does not satisfy this requirement.

## Results and scope

Probes on 2026-11-14 used the routing code from main `81d7b4e`, stable core
1.1.12, and these environments:

| Environment | TCP, local-IP or wildcard listener | UDP, local-IP listener | UDP, wildcard listener |
| --- | --- | --- | --- |
| Windows Server 2022, Wintun 0.14.1, IPv4/IPv6 | Direct reply | Direct reply | Selects VPN source/interface |
| macOS 15, utun, IPv4/IPv6 | Direct reply | Direct reply | Selects VPN source/interface |
| Android API 35 emulator, production VpnService, IPv4 | Direct reply | Reply times out | Reply times out |

[Recorded desktop and Android measurements, including new connections from
the server application](https://github.com/dimmmmmmmer/freetunnel/actions/runs/43639402686).
Android logs confirm that both UDP requests reach the server and its send
calls complete. With the server application's UID excluded, all four incoming
cases work. That comparison diagnoses the interception; it is not a proposed
change to production defaults.

Windows/macOS tests inject packets through an isolated uplink interface and
observe actual kernel TCP/UDP responses with a more-specific VPN route in
place. They do not run the full FreeTunnel application or encrypted VPN
transport. macOS needs the interface-scoped uplink route that a normal
physical uplink has; omitting it from a synthetic test causes false failures.
Windows testing permits the test ports through the host firewall. FreeTunnel's
own Windows kill switch deliberately blocks incoming connections unless the
VPN config's allowed ports are enabled; these tests do not disable that policy
in the application.

The Android test starts the real production service and native core. A local
TLS sink holds the upstream connection attempt while the VPN routes are
installed; this is **not** a successfully authenticated VPN session. A separate
application UID hosts the servers because FreeTunnel excludes its own UID.
The emulator forwards host traffic to `10.0.2.15`; Wi-Fi is disabled to avoid
confusing it with `10.0.2.16`. The test explicitly captures peer `10.0.2.2/32`
instead of allowing the default LAN exclusion to mask the problem. Keep the
server Activity visible so Android's cached-process freezing does not create
false failures. New outgoing UDP is checked from both the server UID and a
second control UID. The app-bypass comparison demonstrates why it is unsuitable
when the same server's new connections must remain captured.

The Android result is IPv4 on one emulator API level. Physical Android
devices, IPv6, carrier NAT, Windows desktop editions and a connected live VPN
endpoint are not covered by this investigation.

The Linux 1.2.5 conntrack fix has the same wildcard UDP limitation as the
desktop probes. An isolated Linux comparison with actual tunnel IP addresses
passes for local-IP UDP listeners, but wildcard UDP can select the tunnel's
source address before the reply rule matches. The release wording therefore
specifies local-IP UDP listeners. A server on multiple interfaces should bind
the intended IP or preserve each request's destination when choosing its reply
source address.

## Why there is no automatic Android UDP fix here

Android's [socket marking code](https://android.googlesource.com/platform/system/netd/+/refs/tags/android-14.0.0_r34/server/FwmarkServer.cpp)
handles accepted TCP sockets using the network marking supplied by the kernel;
the corresponding UDP send callbacks do not establish an incoming-flow reply
exception. The emulator measurements are consistent with that distinction.

[`VpnService.protect`](https://developer.android.com/reference/android/net/VpnService#protect(int))
operates on an available socket descriptor. FreeTunnel does not possess the
server socket descriptors of unrelated applications, nor does its ordinary
VPN permission grant the privileged Linux firewall/routing operations used by
the Linux patch. A UDP packet reaching the TUN alone also does not establish
that it is a reply: it could be a new outgoing datagram. Reclassifying all
traffic from a server UID, port or physical source IP could bypass new traffic.

An [application exclusion](https://developer.android.com/reference/android/net/VpnService.Builder#addDisallowedApplication(java.lang.String))
is broader than an incoming-only exception. No automatic exclusion, global
bypass permission, kill-switch relaxation, or root requirement is introduced.
Transparent incoming UDP with new outgoing traffic kept in VPN remains an
unresolved Android requirement. Cooperating server applications or a separately
designed privileged implementation would need their own investigation.

## Repeating the probes

Use **Incoming routing investigation** via `workflow_dispatch`. Supply
`core_run_id` from a successful workflow in this repository containing the
`android-patched-core` artifact for the current native source/patch set. The
manifest verifier rejects stale or changed cores. This reuses an already
checked artifact instead of starting the full native build or hourly CI suite.
For the recorded source, run `43630686639` supplied that artifact; choose a new
matching run once its artifact expires or the core changes.

The workflow is manual and uses disposable runners. It emits JSON measurements
and Android logcat/instrumentation logs. By default, a successful workflow
means the diagnostic and its control checks ran successfully; **it does not
mean Android UDP is fixed**. Set `require_android_udp=true` to fail the workflow
unless incoming UDP works with the server still included in VPN. The Android
job summary explicitly reports that result.

For local reproduction on disposable machines:

- macOS: `sudo python3 tests/incoming_os_probe.py`.
- Windows: set `WINTUN_DLL` to the verified x64 Wintun 0.14.1 DLL, then run
  `python tests/incoming_os_probe.py` from an administrator terminal.
- Android: follow the emulator setup in `.github/workflows/incoming-routing.yml`,
  including `-PincomingProbe` when installing the test APKs, then run
  `python3 tests/incoming_android_probe.py`. Set `REQUIRE_ANDROID_UDP=true` for
  the strict incoming-UDP requirement. The instrumentation holder is skipped
  in the ordinary Android suite unless `incoming_probe=true` is supplied.

Only benchmark/documentation prefixes are routed on the desktop runners. No
default route, DNS setting, or pre-existing firewall rule is changed. The
Android fixture module is absent from ordinary builds and release APKs.
