# Server switching without interrupting direct connections

Status: **desktop integration implemented; cross-platform validation in progress**.
Desktop manual server selection now prepares a replacement before committing it
inside the existing session. Android and the separate iOS branch still need their
platform adapters wired to this operation. Do not advertise all-platform seamless
switching yet.

The required behavior is to retain the TUN device, direct TCP/UDP sockets and
split-routing policy while changing the VPN endpoint. VPN traffic must retain
its configured kill-switch protection; blocking all outbound traffic during a
switch is not acceptable.

## Implemented foundation

`vpn_replace_endpoint` validates the replacement before disconnecting the old
endpoint. It runs the change on the existing core event loop, cancels old ping
and recovery work, replaces credentials and endpoints, and starts recovery. It
does not call `vpn_stop`, remove OS routes, reset direct connections. Optional DNS upstreams and a versioned routing
snapshot are validated and committed with the endpoint. An optional synchronous commit callback lets adapters update
endpoint certificate policy after the old handshake stops and before the new
one starts. Callers must not re-enter VPN APIs from that callback.

Tests cover repeated replacement, invalid configuration retaining the old
endpoint, explicit direct requests with the kill switch enabled during the
switch, and replacement after stop. The existing tunnel tests cover established
and pending direct TCP/UDP flows through endpoint disconnect/recovery.

The recovery FSM also handles a network returning before the first successful
connection under the unlimited recovery policy. Previously that case selected
the finite-attempt path (and asserted in debug builds).

TLS sessions are scoped to a random endpoint-configuration identity plus the
endpoint address, remote ID, SNI and transport. Recovery keeps the identity;
replacement generates a fresh one. Late tickets retain their original identity,
and calls without an explicit scope do not resume sessions. This prevents a
cached TLS session from skipping a changed certificate policy, including for
profiles with the same hostname. TLS 1.2 and TLS 1.3 memory-BIO tests exercise real
handshakes with accepting and rejecting certificate callbacks. Sessions from a
previous process are intentionally not reused because that process's policy
identity no longer exists.

## Desktop integration

Endpoint preparation owns its configuration and resolves addresses away from the
helper event loop. It never borrows the live client. Cancellation invalidates the
result; rapid picks coalesce to the latest request. A failed preparation leaves
the old server and policy intact and restores its selection in the UI.

The connect IPC message carries the complete routing snapshot and a request ID.
The core commits domain rules and the application's decision generation at one
boundary. A live rule edit made during preparation takes priority over the staged
snapshot. Queued state callbacks from the previous endpoint are discarded. A fatal
new endpoint handshake retains the direct side and configured kill switch until
an explicit stop or another server selection.

Endpoint sockets are protected individually instead of installing endpoint-wide
IP bypasses. Changes that require rebuilding the listener (MTU, OS routes or
kill-switch settings) are rejected by endpoint replacement. Explicit settings
edits continue to use the existing full rebuild path. Actual routing-policy changes
retain the existing connection-reset behavior; the no-interruption guarantee here
covers server changes with unchanged routing rules.

Local validation: 23 core state-machine cases, 7 real wrapper tests, 67 Qt adapter
cases and desktop/helper integration suites. The direct-DNS fix has a separate
207-case native DNS run. These checks do not substitute for real device testing.

## Remaining validation and mobile work

- Preserve desktop OS routes without leaving obsolete endpoint-wide bypasses;
  verify protected endpoint sockets on Linux, macOS and Windows.
- Wire Android and iOS adapters to the operation with cancellation, certificate
  policy and routing snapshots appropriate to their existing sessions.
- Exercise continuous direct TCP/UDP transfer, VPN traffic and DNS during rapid
  switching, failed handshakes and cancellation in isolated platform tests.

Do not replace these checks with a temporary blanket firewall block, a second
simultaneous auto-configured TUN, or a live-network experiment on the user's host.
