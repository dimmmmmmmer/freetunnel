# Desktop Qt

FreeTunnel uses Qt **6.9.3**, built from source for all three desktop platforms.
This is the last public 6.9 release supporting macOS 12. The stock binaries are
not used: `qtbase.patch`, `qtdeclarative.patch` and `qtsvg.patch` retain networking
fixes and incorporate the relevant published Qt security backports below.

`source.json` pins each module to the commit behind its v6.9.3 tag and verifies
the SHA-256 of its source archive before extraction. Build with:

```sh
python3 scripts/build-qt.py --prefix /absolute/path/to/qt --work /absolute/path/to/qt-build
```

Requires Python with tarfile's data filter (3.12+, or a maintained 3.10/3.11),
CMake, Ninja and a C++17 compiler. CI's `.github/actions/qt` installs platform
dependencies and caches the installed SDK by platform and the full recipe.
macOS builds both arm64 and x86_64 slices with deployment target 12.0. Windows
uses MSVC 2022. Linux keeps XCB, D-Bus and the OpenSSL TLS plugin. Translation
tools and Qt's Russian catalogs are built from the same pinned release.

## Backport sources

The consolidated patches retain Qt's original licenses and copyrights; they
are not licensed under FreeTunnel's Apache license. Original upstream changes
and author/reviewer metadata can be found in these official patch collections:

- [Qt 6.9 patches](https://download.qt.io/archive/qt/6.9/):
  CVE-2025-12385 (both Qt Quick text/image fixes).
- [Qt 6.11 patches](https://download.qt.io/official_releases/qt/6.11/):
  CVE-2026-105923 (triangulator), 105924 (CBOR/JSON), 105946 (all three
  authenticator fixes), 107157 (CBOR), 16762 (both XML parser fixes),
  19248 (XML destruction), 6210 (SVG node types), 76151 (HTTP headers),
  78253 (XML text recursion), 79616 (SVG paths), 82292 (XML null nodes),
  95620 (HTTP/2 upload lifetime).
- [Qt 6.8 backports](https://download.qt.io/archive/qt/6.8/):
  CVE-2026-116337 (both status line and header block limits), 96432 (HTTP/2 null stream), and the
  updated ten-patch CVE-2026-8168 SVG recursion series.

The 2025 SVG fixes 10728/10729 are already in 6.9.3. The advisory's 101201
patches target Qt5Compat's binary JSON parser, which is not built or shipped.
Modules such as WebEngine, WebSockets, MQTT, NFC, CoAP and Qt3D are not included.

Context adjustments preserve 6.9's SVG animation support, its TinyDocument
class name and existing APIs. XML and HTTP/2 backports adjust only surrounding
context. No security assertion or connection test is relaxed for the downgrade.

Networking regression fixes from [qtbase](https://github.com/qt/qtbase):

- `9c1e2057ccbe516ef3d02053b4512b532e5f42ca` and
  `e09fcc87f0f16c9ce6413d81b4e752fc7dec82f3`: Unix socket error handling.
- [`949bebaec69d908ca842e8ef7d3e3800d8c2f7b5`](https://github.com/qt/qtbase/commit/949bebaec69d908ca842e8ef7d3e3800d8c2f7b5):
  complete HTTP replies when the peer closes TLS without a Content-Length.
  This prevents Schannel from turning a closed 401 response or an empty
  server reply into a timeout. Existing native-TLS tests cover both cases.
- `69e1037be2`, `d3bff721a9`, `2e4ab09ba3`, `9d57800a8f`, `4db11b10bb`:
  Schannel SNI encoding, certificate name validation, concurrent handshakes,
  certificate lifetime and leaf-certificate selection.

SVG allocation fix from [qtsvg commit 6d74ff9892](https://github.com/qt/qtsvg/commit/6d74ff9892b7484132f416171c62edbfd015e9fc):
Gaussian blur checks its working buffer against the image allocation limit.
Adapted to 6.9's `QVarLengthArray<uint64_t>` instead of the newer `ColorValues`
buffer; widened multiplication prevents integer overflow.

The application keeps its TLS, connection-error and certificate-pinning tests.
Before release, run all desktop tests, inspect rendered UI previews, and audit
every Mach-O dependency with `scripts/check-macos-target.py --target 12.0`.
The SDK/target audit does not substitute for a launch and VPN test on macOS 12.
