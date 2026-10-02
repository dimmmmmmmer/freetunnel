# Deep links: `tt://` and `freetunnel://`

The reference for the two kinds of link FreeTunnel handles: `tt://` links,
which import a VPN configuration, and `freetunnel://` links, which control the
app. It is for anyone writing or debugging an import link, driving FreeTunnel
from a script or a Stream Deck, or working on the code that reads them.

## Contents

- [Overview](#overview)
- [The `tt://` link format](#the-tt-link-format)
  - [URI format](#uri-format)
  - [TLV records](#tlv-records)
  - [Field tags](#field-tags)
  - [Upstream protocol (tag `0x09`)](#upstream-protocol-tag-0x09)
  - [Client random (tag `0x0B`)](#client-random-tag-0x0b)
  - [Display name (tag `0x0C`)](#display-name-tag-0x0c)
  - [DNS upstream list (tag `0x0D`)](#dns-upstream-list-tag-0x0d)
- [Validation rules](#validation-rules)
- [How a link becomes a config](#how-a-link-becomes-a-config)
- [FreeTunnel control links (separate)](#freetunnel-control-links-separate)
  - [Commands](#commands)
  - [Run as a command, or opened as a link](#run-as-a-command-or-opened-as-a-link)
  - [Which links ask first](#which-links-ask-first)
  - [Run FreeTunnel from a script or a Stream Deck](#run-freetunnel-from-a-script-or-a-stream-deck)
  - [When FreeTunnel is already running](#when-freetunnel-is-already-running)
  - [How a link is told from a command](#how-a-link-is-told-from-a-command)
- [Examples](#examples)
- [Tests](#tests)

## Overview

| Scheme | What it does | Encoding | Code |
| --- | --- | --- | --- |
| `tt://` | Imports a VPN configuration. It is the **TrustTunnel deep link**, the same format QR codes and mobile clients use. | base64url of TLV records | `src/core/DeepLink.cpp`, `include/core/DeepLink.h` |
| `freetunnel://` | Controls the app: toggle, connect, disconnect. | A plain verb, not TLV | `ControlCommand.cpp` |

## The `tt://` link format

### URI format

```text
tt://?<base64url-payload>
```

- The scheme is `tt`, and it is case-sensitive.
- The payload is the query body, after the `?`.
- The payload is encoded as **base64url** (RFC 4648), with no padding.

Share links that embed the same payload are accepted too, for example:

```text
https://trusttunnel.org/qr.html#tt=<base64url>
?tt=<base64url>
```

### TLV records

The decoded payload is a sequence of **type-length-value** records, one after
another. Each record:

| Part | Encoding |
| --- | --- |
| Tag | QUIC varint |
| Length | QUIC varint (byte length of value) |
| Value | Raw bytes |

- Both tag and length are **QUIC variable-length integers** (RFC 9000 §16), not
  fixed-width fields.
- Unknown tags are ignored, for forward compatibility.
- The maximum supported format version is **1** (`kDeepLinkMaxVersion`).

### Field tags

| Tag | Field | Type | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| `0x00` | Version | varint | no | `0` | At most `1` is accepted |
| `0x01` | Hostname | UTF-8 string | **yes** | — | |
| `0x02` | Address | UTF-8 string `host:port` | **yes** (≥1) | — | One record per address |
| `0x03` | Custom SNI | UTF-8 string | no | empty | |
| `0x04` | Allow IPv6 | 1 byte bool | no | `true` | |
| `0x05` | Username | UTF-8 string | **yes** | — | |
| `0x06` | Password | UTF-8 string | **yes** | — | Kept in the OS credential store, see [How a link becomes a config](#how-a-link-becomes-a-config) |
| `0x07` | Skip verification | 1 byte bool | no | `false` | |
| `0x08` | Certificate | concatenated DER | no | empty | A chain: each certificate's DER follows the last |
| `0x09` | Upstream protocol | varint | no | `1` (HTTP/2) | See [values](#upstream-protocol-tag-0x09) |
| `0x0A` | Anti-DPI | 1 byte bool | no | `false` | |
| `0x0B` | Client random prefix | UTF-8 `prefix[/mask]` hex | no | empty | See [rules](#client-random-tag-0x0b) |
| `0x0C` | Display name | UTF-8 string | no | empty | See [naming](#display-name-tag-0x0c) |
| `0x0D` | DNS upstreams | string list | no | empty | See [encoding](#dns-upstream-list-tag-0x0d) |

### Upstream protocol (tag `0x09`)

| Value | Protocol |
| --- | --- |
| `1` | HTTP/2 |
| `2` | HTTP/3 |

### Client random (tag `0x0B`)

A UTF-8 string, `prefix` or `prefix/mask`, in hex. The slash separates the
prefix from the mask. FreeTunnel writes it to the config's `client_random`, in
the form the core reads, after the repairs below.

A value is accepted when the prefix and the mask are each:

- whole bytes in hex: an even number of digits, in either case;
- at most 32 bytes (64 digits), the size of a TLS client random.

What the import does with a value:

| Value in the link | Example | Result |
| --- | --- | --- |
| Whole bytes of hex, with or without a mask | `deadbeef`, `DeadBeef/00ff` | Written to `client_random` as it is |
| Trailing slashes | `deadbeef/`, `aa//` | Repaired: the slashes are dropped, leaving the prefix alone |
| A mask with no prefix | `/ffff` | Repaired: the field is dropped |
| Any other value | `abc`, `deadbeef/fff` | The import fails |

A config's share link never carries a value its import would refuse:

- When the value as a whole would be refused but its prefix would not, as with a
  mask of an odd number of digits, the link carries the prefix alone.
- Any other such value is left out of the link.

The config editor checks a value by the same rule but repairs neither edge case.
It refuses trailing slashes and a mask with no prefix, and asks you to fix the
value.

Why it works this way:

- `client_random` is the one key the TrustTunnel core looks in. The core splits
  it at the slash itself and has no separate mask key.
- The core decodes each part as bytes and does without one it cannot decode. It
  uses no more than 32 bytes, so nothing else is of use to it.
- Trailing slashes are an empty mask, for which the core rejects the whole
  config.
- A mask with no prefix has nothing to mask, and the core sends a random client
  random of its own.
- A share link can carry the prefix alone because the core does without a mask
  it cannot decode and sends the prefix unmasked. Whoever imports the link
  connects as the config does.

### Display name (tag `0x0C`)

- The name the config is listed under, which is also its file name.
- A link without one is named after its hostname.
- Either is cut to 50 characters, the most a config name can have.

### DNS upstream list (tag `0x0D`)

The value is a concatenation of entries. Each entry is a `varint length`
followed by that many UTF-8 bytes. The example
[A DNS upstream list](#a-dns-upstream-list) shows the bytes of one.

## Validation rules

Import fails when:

- the URI is not `tt://…`;
- the base64url payload is invalid or empty;
- the TLV stream is truncated, or a length overflows the payload;
- `version > 1`;
- the hostname, the username or the password is missing, or there is no
  address;
- the DNS upstream list is malformed;
- the client random is not `prefix[/mask]` in whole bytes of hex (see
  [tag `0x0B`](#client-random-tag-0x0b)).

Unknown tags are not an error: they are ignored.

## How a link becomes a config

`deepLinkConfigToToml()` maps the parsed link to TrustTunnel client TOML,
matching what the in-app create-config form produces.

- **Control characters are stripped** from the string fields, to prevent TOML
  injection from crafted links.
- **Passwords go to the OS credential store**, not the on-disk TOML.
- **The display name** names the config and its file (see
  [tag `0x0C`](#display-name-tag-0x0c)).
- **Every import asks first**, however the link arrives (see
  [Which links ask first](#which-links-ask-first)).

Each field's TOML key is below. The keys are in the `[endpoint]` table,
except `dns_upstreams`, which is at the top level.

| Tag | Field | TOML key |
| --- | --- | --- |
| `0x00` | Version | Not written |
| `0x01` | Hostname | `hostname` |
| `0x02` | Address | `addresses`: every address, in one list |
| `0x03` | Custom SNI | `custom_sni` |
| `0x04` | Allow IPv6 | `has_ipv6` |
| `0x05` | Username | `username` |
| `0x06` | Password | `password`, which the import then moves to the OS credential store |
| `0x07` | Skip verification | `skip_verification` |
| `0x08` | Certificate | `certificate`, as PEM: one `BEGIN CERTIFICATE` block per certificate |
| `0x09` | Upstream protocol | `upstream_protocol`: `"http2"` or `"http3"` |
| `0x0A` | Anti-DPI | `anti_dpi` |
| `0x0B` | Client random | `client_random`, whole, as `prefix[/mask]` |
| `0x0C` | Display name | Not a key: the config's name and file name |
| `0x0D` | DNS upstreams | `dns_upstreams` |

## FreeTunnel control links (separate)

Application control uses a different scheme, `freetunnel://`, which is not TLV.
It is handled by `ControlCommand.cpp`.

### Commands

| URI | Action |
| --- | --- |
| `freetunnel://toggle` | Toggle VPN |
| `freetunnel://connect` | Connect |
| `freetunnel://disconnect` | Disconnect |

- The verb is case-insensitive.
- Slashes after the scheme are ignored: `freetunnel://toggle/` is a toggle.
- A disconnect given while "Connect on startup" has not yet connected calls that
  connection off.

### Run as a command, or opened as a link

The same URL reaches FreeTunnel in two ways, and they are not trusted alike.

| | Run as a command | Opened as a link |
| --- | --- | --- |
| What it is | The program started with the URL as its argument | The URL opened through the system's URL handling |
| Examples | A Stream Deck button, a launcher or a script | A browser, a document, the Windows Run box, `xdg-open`, `open` on macOS, or a Stream Deck action that opens a URL |

- A Stream Deck button, a launcher or a script should run the URL as a command.
- A running FreeTunnel acts on a command at once, without bringing up the
  window, as with the global hotkeys. Otherwise FreeTunnel starts and acts on
  it.
- A link may ask first: see [Which links ask first](#which-links-ask-first).

### Which links ask first

A link that would turn the VPN off brings up the window and asks first,
because any web page can open a link. Run as a command, the same URL acts at
once. `tt://` imports always ask, however they arrive.

| Opened as a link | Result |
| --- | --- |
| `freetunnel://connect` | Goes ahead |
| `freetunnel://toggle` that connects | Goes ahead |
| `freetunnel://toggle` while connected or connecting | Asks first |
| `freetunnel://disconnect` while connected or connecting | Asks first |
| `freetunnel://disconnect` while "Connect on startup" is still about to connect | Asks first |
| `tt://` import | Asks first, as it does when run as a command |

- Only a click answers the question, and only once it has been up for a moment.
  Return does not.
- `tt://` imports are answered the same way.

### Run FreeTunnel from a script or a Stream Deck

Run the program with the URL as its argument. For a Stream Deck button, use
this command, not an action that opens the URL: that would be a link. Replace
`toggle` with `connect` or `disconnect` as needed.

Windows (use your install folder if it is not this one):

```bat
"C:\Program Files\FreeTunnel\FreeTunnel.exe" freetunnel://toggle
```

macOS:

```sh
/Applications/FreeTunnel.app/Contents/MacOS/FreeTunnel freetunnel://toggle
```

On macOS, `open freetunnel://toggle` goes through LaunchServices, so it is a
link, not a command.

Linux, installed from the .deb:

```sh
freetunnel freetunnel://toggle
```

Linux, AppImage:

```sh
/path/to/freetunnel-x86_64.AppImage freetunnel://toggle
```

### When FreeTunnel is already running

- A launch that finds FreeTunnel already running for the same user forwards the
  command to it over a local socket, and exits. That is `InstanceControl.cpp`;
  [docs/security-threats.md](docs/security-threats.md) has the details.
- If that copy is running but cannot be handed the command, as when the keyring
  holding the socket's token is locked, the launch exits without acting on it
  rather than start a second copy. Run it again.

### How a link is told from a command

The URL handler registrations start the app with `--url-handler` before the
URL:

```text
FreeTunnel --url-handler <url>
```

| OS | Registration |
| --- | --- |
| Windows | The installer's `freetunnel` and `tt` keys under `HKLM\Software\Classes` |
| Linux | The `.desktop` file of the .deb and of the AppImage |
| macOS | None on the command line: macOS hands links over as Apple events (`QFileOpenEvent`), and every one is a link |

- A link is passed on as a marked control string (`linkControlString()`), so it
  is still a link once forwarded to the running instance.
- A registration made some other way, without `--url-handler`, makes links look
  like commands. That includes a menu entry AppImageLauncher or Gear Lever made
  from an older AppImage.

## Examples

### A minimal import link

```text
tt://?AAEBAQ92cG4uZXhhbXBsZS5jb20CEDIwMy4wLjExMy4xMDo0NDMFBWFsaWNlBgZzZWNyZXQMB0V4YW1wbGU
```

Decoded, the payload is six records. Every tag and length here is below 64, so
each varint is a single byte:

| Tag | Length | Value | Field |
| --- | --- | --- | --- |
| `00` | `01` | `01` | Version 1 |
| `01` | `0f` | `vpn.example.com` | Hostname |
| `02` | `10` | `203.0.113.10:443` | Address |
| `05` | `05` | `alice` | Username |
| `06` | `06` | `secret` | Password |
| `0c` | `07` | `Example` | Display name |

FreeTunnel's own share links are written this way: version 1 first, then the
required fields, then only the optional fields that differ from their default.
The same payload in a share link:

```text
https://trusttunnel.org/qr.html#tt=AAEBAQ92cG4uZXhhbXBsZS5jb20CEDIwMy4wLjExMy4xMDo0NDMFBWFsaWNlBgZzZWNyZXQMB0V4YW1wbGU
```

### A DNS upstream list

A tag `0x0D` record holding `8.8.8.8` and `1.1.1.1`, in hex:

```text
0d 10  07 38 2e 38 2e 38 2e 38  07 31 2e 31 2e 31 2e 31
```

That is tag `0x0D`, length 16, then two entries: length 7 and `8.8.8.8`, length
7 and `1.1.1.1`.

## Tests

| What | Where |
| --- | --- |
| Round-trip encode/decode, injection stripping and edge cases | `tests/test_deeplink.cpp`, `tests/test_configimport.cpp` |
| Control link parser and the link mark | `tests/test_control.cpp` |
| `--url-handler`, macOS link events and forwarding | `tests/test_app_startup.cpp` |
| The socket a launch forwards over | `tests/test_instance_control.cpp`, `tests/test_integration_single_instance.cpp` |
| What a link may do | `tests/test_integration_backend_vpn.cpp` |
| The question a link asks | `tests/test_qml_ui.cpp` |
