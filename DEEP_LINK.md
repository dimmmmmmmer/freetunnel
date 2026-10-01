# TrustTunnel deep links (`tt://`)

FreeTunnel imports VPN configurations from **TrustTunnel deep links** — the same
format used by QR codes and mobile clients.

## URI format

```text
tt://?<base64url-payload>
```

- Scheme is case-sensitive: `tt`
- Payload is the query body (after `?`), encoded as **base64url** (RFC 4648,
  no padding).
- Share links that embed the same payload are also accepted, e.g.
  `https://trusttunnel.org/qr.html#tt=<base64url>` or `?tt=<base64url>`.

Implementation: `src/core/DeepLink.cpp`, `include/core/DeepLink.h`.

## Binary payload: TLV encoding

The decoded payload is a sequence of **type-length-value** records. Both tag and
length use **QUIC variable-length integers** (RFC 9000 §16), not fixed-width
fields.

Each record:

| Field | Encoding |
| --- | --- |
| Tag | QUIC varint |
| Length | QUIC varint (byte length of value) |
| Value | Raw bytes |

Unknown tags are ignored (forward compatibility). Maximum supported format
version: **1** (`kDeepLinkMaxVersion`).

## Field tags

| Tag | Field | Type | Required | Default |
| --- | --- | --- | --- | --- |
| `0x00` | Version | varint | no | `0` |
| `0x01` | Hostname | UTF-8 string | **yes** | — |
| `0x02` | Address | UTF-8 string `host:port` | **yes** (≥1) | — |
| `0x03` | Custom SNI | UTF-8 string | no | empty |
| `0x04` | Allow IPv6 | 1 byte bool | no | `true` |
| `0x05` | Username | UTF-8 string | **yes** | — |
| `0x06` | Password | UTF-8 string | **yes** | — |
| `0x07` | Skip verification | 1 byte bool | no | `false` |
| `0x08` | Certificate | concatenated DER | no | empty |
| `0x09` | Upstream protocol | varint | no | `1` (HTTP/2) |
| `0x0A` | Anti-DPI | 1 byte bool | no | `false` |
| `0x0B` | Client random prefix | UTF-8 `prefix[/mask]` hex | no | empty |
| `0x0C` | Display name | UTF-8 string | no | empty |
| `0x0D` | DNS upstreams | string list | no | empty |

### Tag `0x09` — upstream protocol

| Value | Protocol |
| --- | --- |
| `1` | HTTP/2 |
| `2` | HTTP/3 |

### Tag `0x0D` — DNS upstream list

Value is a concatenation of entries: each entry is `varint length` + UTF-8 bytes.

### Tag `0x0B` — client random

UTF-8 string `prefix` or `prefix/mask` (hex). Slash separates prefix and mask.
FreeTunnel writes it to the config's `client_random` in the form the core reads,
after the repairs listed below. That one key is where the TrustTunnel core looks
(it splits at the slash itself and has no separate mask key).

The prefix and the mask are each whole bytes in hex (an even number of digits,
either case), at most 32 bytes (64 digits), the size of a TLS client random. The
core decodes each part as bytes and does without one it cannot decode, and uses
no more than 32 bytes, so nothing else is of use to it. Two edge cases are
repaired rather than refused:

- Trailing slashes (`deadbeef/`, `aa//`): an empty mask, for which the core
  rejects the whole config. They are dropped, leaving the prefix alone.
- A mask with no prefix (`/ffff`): there is nothing for it to mask, and the core
  sends a random client random of its own. The field is dropped.

Any other value makes the import fail.

A config's share link never carries a value its import would refuse. When the
value as a whole would be refused but its prefix would not, as with a mask of an
odd number of digits, the link carries the prefix alone: the core does without a
mask it cannot decode and sends the prefix unmasked, so whoever imports the link
connects as the config does. Any other such value is left out.

The config editor checks a value by the same rule but repairs neither edge case.
It refuses trailing slashes and a mask with no prefix, and asks you to fix the
value.

### Tag `0x0C` — display name

The name the config is listed under, which is also its file name. A link without
one is named after its hostname. Either is cut to 50 characters, the most a
config name can have.

## Validation rules

Import fails when:

- URI is not `tt://…`
- Base64url payload is invalid or empty
- TLV stream is truncated or length overflows payload
- `version > 1`
- Missing hostname, any address, username, or password
- Malformed DNS upstream list
- Client random that is not `prefix[/mask]` in whole bytes of hex (see tag `0x0B`)

Passwords from deep links are stored in the OS credential store, not in the
on-disk TOML.

## TOML generation

`deepLinkConfigToToml()` maps the parsed config to TrustTunnel client TOML
(matching the in-app create-config form). Control characters in string fields
are stripped to prevent TOML injection from crafted links.

## FreeTunnel control links (separate)

Application control uses a different scheme (not TLV):

| URI | Action |
| --- | --- |
| `freetunnel://toggle` | Toggle VPN |
| `freetunnel://connect` | Connect |
| `freetunnel://disconnect` | Disconnect |

Handled by `ControlCommand.cpp`. A launch that finds FreeTunnel already running
for the same user forwards the command to it over a local socket
(`InstanceControl.cpp`; [docs/security-threats.md](docs/security-threats.md)
has the details) and exits. If that copy is running but cannot be handed the
command, as when the keyring holding the socket's token is locked, the launch
exits without acting on it rather than start a second copy; run it again. The
verb is case-insensitive and slashes after the scheme are ignored. A disconnect
given while "Connect on startup" has not yet connected calls that connection
off.

### Run as a command, or opened as a link

The same URL reaches FreeTunnel in two ways, and they are not trusted alike:

- **Run as a command** — the program started with the URL as its argument. This
  is what a Stream Deck button, a launcher or a script should do. A running
  FreeTunnel acts on it at once, without bringing up the window, as with the
  global hotkeys; otherwise FreeTunnel starts and acts on it.
- **Opened as a link** — through the system's URL handling: a browser, a
  document, the Windows Run box, `xdg-open`, `open` on macOS, or a Stream Deck
  action that opens a URL. Any web page can open a link, so a link that would
  turn the VPN off — `disconnect`, or `toggle` while connected or connecting —
  brings up the window and asks first. So does a `disconnect` link while
  "Connect on startup" is still about to connect. Only a click answers the
  question, and only once it has been up for a moment; Return does not.
  `connect` and a `toggle` that connects go ahead; `tt://` imports always ask,
  and are answered the same way.

| OS | Run as a command |
| --- | --- |
| Windows | `"C:\Program Files\FreeTunnel\FreeTunnel.exe" freetunnel://toggle` (or your install folder) |
| macOS | `/Applications/FreeTunnel.app/Contents/MacOS/FreeTunnel freetunnel://toggle` |
| Linux (.deb) | `freetunnel freetunnel://toggle` |
| Linux (AppImage) | `/path/to/freetunnel-x86_64.AppImage freetunnel://toggle` |

On macOS, `open freetunnel://toggle` goes through LaunchServices, so it is a
link, not a command.

How the two are told apart: the URL handler registrations — the installer's
`freetunnel` and `tt` keys under `HKLM\Software\Classes` on Windows, the
`.desktop` file of the .deb and of the AppImage on Linux — start the app as
`FreeTunnel --url-handler <url>`. macOS hands links over as Apple events
(`QFileOpenEvent`), never on the command line, and every one is a link. A link
is passed on as a marked control string (`linkControlString()`), so it is still
a link once forwarded to the running instance. A registration made some other
way, without `--url-handler`, makes links look like commands; that includes a
menu entry AppImageLauncher or Gear Lever made from an older AppImage.

## Tests

Round-trip encode/decode, injection stripping, and edge cases are covered in
`tests/test_deeplink.cpp` and `tests/test_configimport.cpp`. Control links: the
parser and the link mark in `tests/test_control.cpp`, `--url-handler`, macOS
link events and forwarding in `tests/test_app_startup.cpp`, the socket a
launch forwards over in `tests/test_instance_control.cpp` and
`tests/test_integration_single_instance.cpp`, what a link may do in
`tests/test_integration_backend_vpn.cpp`, the question in `tests/test_qml_ui.cpp`.
