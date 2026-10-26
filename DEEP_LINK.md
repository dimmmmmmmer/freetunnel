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
  - [Certificate (tag `0x08`)](#certificate-tag-0x08)
  - [Upstream protocol (tag `0x09`)](#upstream-protocol-tag-0x09)
  - [Client random (tag `0x0B`)](#client-random-tag-0x0b)
  - [Display name (tag `0x0C`)](#display-name-tag-0x0c)
  - [DNS upstream list (tag `0x0D`)](#dns-upstream-list-tag-0x0d)
  - [Subscription address (tag `0x0E`)](#subscription-address-tag-0x0e)
  - [Client random auth key (tag `0x0F`)](#client-random-auth-key-tag-0x0f)
  - [Versions](#versions)
- [Validation rules](#validation-rules)
- [How a link becomes a config](#how-a-link-becomes-a-config)
  - [The endpoint's TOML export](#the-endpoints-toml-export)
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
- FreeTunnel reads format versions up to **2** (`kDeepLinkMaxVersion`) and
  writes version **1** (`kDeepLinkWriteVersion`); see [Versions](#versions).

### Field tags

| Tag | Field | Type | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| `0x00` | Version | varint | no | `0` | At most `2` is accepted |
| `0x01` | Hostname | UTF-8 string | **yes** \* | — | |
| `0x02` | Address | UTF-8 string `host:port` | **yes** \* (≥1) | — | One record per address |
| `0x03` | Custom SNI | UTF-8 string | no | empty | |
| `0x04` | Allow IPv6 | 1 byte bool | no | `true` | |
| `0x05` | Username | UTF-8 string | **yes** \* | — | |
| `0x06` | Password | UTF-8 string | **yes** \* | — | Kept in the OS credential store, see [How a link becomes a config](#how-a-link-becomes-a-config) |
| `0x07` | Skip verification | 1 byte bool | no | `false` | |
| `0x08` | Certificate | concatenated DER | no | empty | A chain: each certificate's DER follows the last. Only certificates are kept, see [rules](#certificate-tag-0x08) |
| `0x09` | Upstream protocol | varint | no | `1` (HTTP/2) | See [values](#upstream-protocol-tag-0x09) |
| `0x0A` | Anti-DPI | 1 byte bool | no | `false` | |
| `0x0B` | Client random prefix | UTF-8 `prefix[/mask]` hex | no | empty | See [rules](#client-random-tag-0x0b) |
| `0x0C` | Display name | UTF-8 string | no | empty | See [naming](#display-name-tag-0x0c) |
| `0x0D` | DNS upstreams | string list | no | empty | See [encoding](#dns-upstream-list-tag-0x0d) |
| `0x0E` | Subscription address | UTF-8 string, `https://` | no | — | Version 2. Not kept, see [rules](#subscription-address-tag-0x0e) |
| `0x0F` | Client random auth key | UTF-8 hex | no | empty | Version 2. A link with one is refused, see [rules](#client-random-auth-key-tag-0x0f) |

\* Unless the link has a subscription address (tag `0x0E`), as the format
allows from version 2 on.

### Certificate (tag `0x08`)

The DER of one or more certificates, one after another: the server's own
certificate, then any that signed it.

- The value has to split into whole DER elements. One that does not fails the
  import as damaged ("malformed certificate"), as in TrustTunnel's own decoder.
- Only the elements with the outer shape of an X.509 certificate are kept:
  `SEQUENCE { SEQUENCE, SEQUENCE, BIT STRING }`. Anything else, such as a
  private key, is left out of the config, and the question before the import
  says "Extra data in the certificate was ignored."
- A value with no certificate in it at all fails the import as damaged. The
  link meant to pin its server, and without the pin, a server with a
  self-signed certificate would fail later, at the handshake.
- A config's share link carries its certificates and nothing else.

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

### Subscription address (tag `0x0E`)

TrustTunnel 1.3 adds subscriptions: an `https://` address the client fetches
to learn the server's current settings. The address carries the VPN username
and password in its userinfo (`https://user:pass@host/…`).

A value is accepted when it:

- starts with `https://`, in lower case, as the format requires;
- is at most 2048 bytes (`kMaxSubscriptionUrlBytes`);
- is UTF-8 with no control characters, spaces or other whitespace, and no
  invisible format characters such as U+200B or U+202E.

Any other value fails the import. The error never quotes the address.

FreeTunnel does not support subscriptions yet. What the import does:

| The link carries | Result |
| --- | --- |
| The address alone, or with only part of a server (a "subscription-only" link) | Refused: "Subscription links aren't supported yet. Ask your provider for a regular link, or update FreeTunnel when subscriptions arrive." |
| The address and a whole server: a hostname, at least one address, a username and a password, none of them empty | The server is imported as the link has it. The question says the server won't update itself, and the address is not kept |

The address is not written anywhere, neither to the config nor to the
credential store, and it is never logged.

### Client random auth key (tag `0x0F`)

A UTF-8 string of whole bytes in hex. The client derives its TLS client random
from this key, and a server with an auth-key rule turns away a client that does
not. The TrustTunnel core FreeTunnel uses cannot do that yet.

- An empty value means the server has no key, and the link imports as usual.
- Any other value, if it is valid hex, is refused: "This server needs a newer
  TrustTunnel core than this version of FreeTunnel uses."
- A value that is not whole bytes of hex fails the import as damaged.

### Versions

| Version | What it added | FreeTunnel |
| --- | --- | --- |
| `0` | No version tag | Read |
| `1` | Tags `0x00`, `0x0C` and `0x0D` | Read and written |
| `2` | Tags `0x0E` and `0x0F`; the other fields become optional when `0x0E` is there | Read |
| `3` and later | — | Refused: "unsupported deep link version" |

- TrustTunnel's endpoint writes version 2 on every link from 1.3.0 on, even a
  link without either new tag.
- FreeTunnel's own share links stay version 1. They never carry the new tags,
  and FreeTunnel 1.2.3 and earlier, like TrustTunnel's clients up to 1.1.11,
  refuse a version 2 link.
- The version is read before any field, so a link of a newer version is
  refused for its version, not for a field that version may spell differently.
- Every version record counts, as in TrustTunnel's own decoder. A link with
  more than one is read as the newest of them, and refused if that is 3 or
  later.
- A version record whose value is not a whole varint, such as an empty one,
  fails the import as damaged. It is not read as version 0.
- Tags from `0x10` on are ignored in version 2 as in version 1: TrustTunnel's
  next tag will be one of them.

## Validation rules

Import fails when:

- the URI is not `tt://…`;
- the base64url payload is invalid or empty;
- the TLV stream is truncated, or a length overflows the payload;
- a version record's value is not a varint, or any version record is above
  `2`;
- the link has no subscription address, and the hostname, the username or the
  password is missing, or there is no address;
- the certificate does not split into whole DER elements, or holds no
  certificate (see [tag `0x08`](#certificate-tag-0x08));
- the DNS upstream list is malformed;
- the client random is not `prefix[/mask]` in whole bytes of hex (see
  [tag `0x0B`](#client-random-tag-0x0b));
- the subscription address is not one FreeTunnel accepts (see
  [tag `0x0E`](#subscription-address-tag-0x0e));
- the client random auth key is not whole bytes of hex.

These links are well formed, and refused with a reason of their own:

- a subscription address without a whole server
  ([tag `0x0E`](#subscription-address-tag-0x0e));
- a non-empty client random auth key
  ([tag `0x0F`](#client-random-auth-key-tag-0x0f)).

A link that is both is refused for its auth key: a subscription would hand out
the same server, which this version cannot connect to either.

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
| `0x08` | Certificate | `certificate`, as PEM: one `BEGIN CERTIFICATE` block per certificate, and nothing else |
| `0x09` | Upstream protocol | `upstream_protocol`: `"http2"` or `"http3"` |
| `0x0A` | Anti-DPI | `anti_dpi` |
| `0x0B` | Client random | `client_random`, whole, as `prefix[/mask]` |
| `0x0C` | Display name | Not a key: the config's name and file name |
| `0x0D` | DNS upstreams | `dns_upstreams` |
| `0x0E` | Subscription address | Not written, anywhere |
| `0x0F` | Client random auth key | Not written: a link with one is not imported |

A TrustTunnel 1.3 config imported from a file is handled the same way:

- An empty `client_random_auth_key`, which the endpoint's export always writes,
  is kept as it is. A non-empty one refuses the import with the message above.
- A subscription address, as `subscription_url` or as an
  `[endpoint.subscription]` table, is left out of the copy FreeTunnel stores, and
  the notice after the import says so. The file you picked is not changed.
- A stored config that holds one already, as one imported by 1.2.3 or earlier
  can, loses it the next time FreeTunnel starts, and nothing else in it
  changes. That is only for the copies in FreeTunnel's own folder: a config kept
  elsewhere is your own file, which TrustTunnel's client may read the address
  from, and keeps it until FreeTunnel next rewrites that file.

A config's `certificate` keeps its certificates and nothing else, however the
config arrives: from a file, from the config editor, or as a link (see
[tag `0x08`](#certificate-tag-0x08)).

- A block is kept when it is labelled `CERTIFICATE` (or `X509 CERTIFICATE`,
  which OpenSSL reads as well) and decodes to one or more certificates. A
  private key, any other block, a damaged block and text between the blocks
  are left out. FreeTunnel 1.1.9 and earlier wrote a link's whole chain into
  one block, and such a block is kept as it is.
- From a file, only the `certificate` lines of the copy FreeTunnel stores
  change, and the notice after the import says "Extra data in the certificate
  was ignored." The file you picked is not changed. The editor says the same
  once it has saved.
- A `certificate` with no certificate in it at all refuses the import ("Not a
  valid TrustTunnel config: malformed certificate"), and the editor refuses to
  save it.
- A stored config that holds anything else in its certificate loses it the next
  time FreeTunnel starts, by the same rules as the subscription address above.
  With no certificate left, its `certificate` is emptied.
- A share link and a TOML export carry the certificates alone, wherever the
  config is kept.

### The endpoint's TOML export

A TrustTunnel endpoint exports a client's config as a link or, with
`--format toml`, as a TOML file. That file has no `[endpoint]` table: the
server's keys are at the top, meant to be pasted under one. FreeTunnel imports
it from file as it would the link with the same server.

- **How it is told from a config.** Only a file whose `[endpoint]` names no
  server is looked at. It is an export when it has no table at all (no table
  header, no dotted key and no inline table anywhere, an array of them
  included), and sets `hostname`, `addresses`, `username` and `password` at the
  top. A config in any spelling of `[endpoint]` is never read as an export. A
  file with the server's keys at the top and a table besides is neither, and is
  refused as before.
- **What its keys become.** Each is read as the same key under `[endpoint]` is
  (see the table above), and `client_random_prefix` as `client_random`.
  Anything else in the file is left out, as a link's unknown tags are.
- **The name.** `name` names the config. The endpoint writes `name = ""` for a
  server it was given no name for, and then the file's name is used.
- **The link's rules.** A non-empty `client_random_auth_key`, and a
  subscription address without a whole server, are refused with the link's
  messages. `subscription_url` is left out, and the question says so. The
  certificate keeps only its certificates (see
  [tag `0x08`](#certificate-tag-0x08)). A client random the core cannot use, a
  certificate with no certificate in it, and no address or username without a
  subscription address all refuse the import ("Not a valid TrustTunnel
  config: …").
- **It asks first,** as a link does: "Add a VPN server from this file?", with
  the server it names, and Replace / Add copy when a config has its name
  already. Nothing is added until you say yes, and the file you picked is not
  changed.

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

### A version 2 link

A link as TrustTunnel's endpoint 1.3 exports it for a server without
subscriptions or an auth key: version 2, then the required fields in the
endpoint's order.

```text
tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQz
```

| Tag | Length | Value | Field |
| --- | --- | --- | --- |
| `00` | `01` | `02` | Version 2 |
| `01` | `0f` | `vpn.example.com` | Hostname |
| `05` | `05` | `alice` | Username |
| `06` | `06` | `secret` | Password |
| `02` | `0b` | `1.2.3.4:443` | Address |

A subscription-only link, which FreeTunnel refuses for now, is two records:
version 2, then tag `0x0E` with the address.

```text
tt://?AAECDjFodHRwczovL2FsaWNlOnMzY3JldEB2cG4uZXhhbXBsZS5jb20vc3Vic2NyaXB0aW9u
```

| Tag | Length | Value | Field |
| --- | --- | --- | --- |
| `00` | `01` | `02` | Version 2 |
| `0e` | `31` | `https://alice:s3cret@vpn.example.com/subscription` | Subscription address |

These and the other version 2 links the tests use are in
`tests/deeplink_v2_vectors.h`, with their bytes.

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
| Version 2 links: what is read, refused and not kept | `tests/test_deeplink.cpp`, `tests/test_configimport.cpp`, `tests/test_backend_deeplink_import.cpp` |
| TrustTunnel 1.3 config files: subscription address and auth key | `tests/test_configtoml.cpp`, `tests/test_backend_deeplink_import.cpp`, `tests/test_backend_config.cpp` |
| Certificates: what is kept, from a link, a file, the editor and at start, and what a share link or export carries | `tests/test_configtoml.cpp`, `tests/test_deeplink.cpp`, `tests/test_configimport.cpp`, `tests/test_backend_deeplink_import.cpp`, `tests/test_backend_config.cpp`, with the shared vectors in `tests/certificate_vectors.h` |
| The endpoint's TOML export: how it is told from a config, what it becomes and what is asked | `tests/test_configtoml.cpp`, `tests/test_configimport.cpp`, `tests/test_backend_deeplink_import.cpp`, with the shared text in `tests/endpoint_export_vectors.h` |
| Control link parser and the link mark | `tests/test_control.cpp` |
| `--url-handler`, macOS link events and forwarding | `tests/test_app_startup.cpp` |
| The socket a launch forwards over | `tests/test_instance_control.cpp`, `tests/test_integration_single_instance.cpp` |
| What a link may do | `tests/test_integration_backend_vpn.cpp` |
| The question a link asks | `tests/test_qml_ui.cpp` |
