# Server switching without interrupting direct connections

Status: **core API implemented; application integration pending**. The current
application still tears down the session when changing servers. Do not describe
this as seamless switching in release notes yet.

The required behavior is to retain the TUN device, direct TCP/UDP sockets and
split-routing policy while changing the VPN endpoint. VPN traffic must retain
its configured kill-switch protection; blocking all outbound traffic during a
switch is not acceptable.

## Implemented foundation

`vpn_replace_endpoint` validates the replacement before disconnecting the old
endpoint. It runs the change on the existing core event loop, cancels old ping
and recovery work, replaces credentials and endpoints, and starts recovery. It
does not call `vpn_stop`, remove OS routes, reset direct connections or change
DNS settings. An optional synchronous commit callback lets adapters update
endpoint certificate policy after the old handshake stops and before the new
one starts. Callers must not re-enter VPN APIs from that callback.

Tests cover repeated replacement, invalid configuration retaining the old
endpoint, explicit direct requests with the kill switch enabled during the
switch, and replacement after stop. The existing tunnel tests cover established
and pending direct TCP/UDP flows through endpoint disconnect/recovery.

The recovery FSM also handles a network returning before the first successful
connection under the unlimited recovery policy. Previously that case selected
the finite-attempt path (and asserted in debug builds).

## Required before enabling application switching

- Resolve the new endpoint and read credentials before modifying the live session.
- Preserve desktop OS routes without leaving obsolete endpoint-wide bypasses;
  verify protected endpoint sockets on Linux, macOS and Windows.
- Update DNS upstreams and certificate policy together with the endpoint. Check
  TLS session-cache isolation when profiles use different certificate policies.
- Wire desktop helper IPC and mobile adapters to the new operation. Keep the
  previous connection on validation failure, and make cancel/newer requests win.
- Apply the intended split-rule generation consistently across a switch, and
  retain direct connections on endpoint authentication/certificate failure.
- Exercise continuous direct TCP/UDP transfer, VPN traffic and DNS during rapid
  switching, failed handshakes and cancellation in isolated platform tests.

Do not replace these checks with a temporary blanket firewall block, a second
simultaneous auto-configured TUN, or a live-network experiment on the user's host.
