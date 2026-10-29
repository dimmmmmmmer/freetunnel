# Android changes

## Unreleased

- Keep split-tunnelling edits intact and report a save failure when Android
  storage refuses to persist a change.

## 1.0.0

The first official FreeTunnel for Android: a TrustTunnel VPN client for phones,
tablets and Android TV, built on TrustTunnel's own Android library 1.1.7. It
needs Android 8.0 or later.

### Highlights

- Add servers from `tt://` links, TrustTunnel's QR pages, TOML files (a client
  config, or the file a TrustTunnel endpoint exports) and the clipboard.
  Nothing is added until you confirm, and importing never connects.
- The kill switch is on by default. If the connection is lost, traffic stays
  blocked and FreeTunnel reconnects by itself: after 1 s, then up to every
  30 s, and at once when a network appears. An alert offers Reconnect and
  Disconnect.
- Split tunnelling by app or by destination (domains and addresses).
- Android TV: FreeTunnel is on the TV launcher, and every control is reachable
  with a remote. **Receive from phone** shows a QR code on the TV; a phone,
  tablet or laptop on the same network opens it in a browser and sends a link
  or a TOML file.
- Updates come from this project's GitHub releases. The app checks for them on
  its own when it starts, at most every 12 hours, and Settings checks when you
  ask. Each is verified before Android is asked to install it.
- A Quick Settings tile, Android's Always-on VPN, and English and Russian.

### Security

- Server configurations are encrypted with a key kept in Android Keystore, and
  are left out of backups and device transfer.
- The configuration editor hides the password, blocks screenshots and screen
  recording, and asks the keyboard neither to learn from what is typed there
  nor to show its own copy of it.
- Of a server's certificate, only the certificates are kept. Anything else in
  it is left out, and the import says so.
- An update must be a newer version, match the checksum in a manifest signed
  with the project's release key, and be signed with the release certificate.
  A build signed with another key never checks for updates.
- Installing an update while the VPN is on asks first, because Android turns
  the VPN off while it replaces the app. FreeTunnel reconnects after the
  update, unless you disconnected in the meantime.
- Receive from phone listens only on the home network, for at most
  5 minutes, behind a one-time code, and stops after 3 wrong codes. The
  link crosses the network unencrypted, so use it on a network you trust. The
  TV shows which address sent the link, and adds nothing until you confirm.

### Not yet

- TrustTunnel 1.3 subscriptions are not followed. A link or file with only a
  subscription is refused. From one that also has its server, the server is
  added, and won't update itself. The subscription address, which holds the
  VPN password, is not kept.
- Servers that need a client random auth key are refused, with a message: they
  need TrustTunnel core 1.3.
