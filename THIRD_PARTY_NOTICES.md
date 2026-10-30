# Third-party notices

FreeTunnel is licensed under the Apache License 2.0, given in full in
[LICENSE](LICENSE). Its downloads also contain software written by others and
licensed under their own terms. This file names each of those components, says
which downloads contain it, and points to the license texts, which are in
[licenses/](licenses/):

- `licenses/common/`: components in every download (the VPN core's libraries and
  the Inter typeface).
- `licenses/desktop/`: components in the Windows, macOS and Linux downloads.
- `licenses/android/`: components in the Android app.

Many components use the Apache License 2.0. Its text is the one in
[LICENSE](LICENSE), and they have no file of their own under `licenses/`.

None of these projects endorses FreeTunnel, and their names are used here only
to say where their code comes from.

## Where to find these notices

Every download carries this file, `LICENSE` and the `licenses/` folder it needs:

- Windows: in the installation folder, next to `FreeTunnel.exe`.
- macOS: in `FreeTunnel.app/Contents/Resources/`.
- Linux .deb: in `/usr/share/doc/freetunnel/`. The Debian copyright files of
  the bundled system libraries are in `/opt/freetunnel/share/doc/<package>/`.
- Linux AppImage: in `usr/share/doc/freetunnel/` inside the image (run it with
  `--appimage-extract` to unpack it), with `appimage-runtime.txt`, and the
  system libraries' copyright files in `usr/share/doc/<package>/`.
- Android: in the APK's `assets/`: `assets/THIRD_PARTY_NOTICES.md`,
  `assets/LICENSE` and `assets/licenses/` (without the desktop's texts). Unzip
  the APK to read them. As on the desktop, they are files, not a screen of the
  app. They are also in the repository.

## What each download contains

### Windows

`freetunnel-windows-x86_64-Setup.exe` installs:

- FreeTunnel (`FreeTunnel.exe`). Compiled into it: [the VPN core and its
  libraries](#the-vpn-core-and-its-libraries) (with
  [wintun.h](#wintun) and [Microsoft Detours](#microsoft-detours), which only
  the Windows build uses), [QHotkey](#qhotkey), [QWindowKit](#qwindowkit), [the
  Feather and Lucide icons](#feather-and-lucide-icons) and the [Inter](#inter)
  typeface.
- Next to it: [Qt 6.11.2](#qt) with [the third-party code inside
  Qt](#third-party-code-inside-qt), [Wintun](#wintun) (`wintun.dll`),
  [Mesa llvmpipe](#mesa-llvmpipe) (`opengl32sw.dll`) and [Microsoft's
  redistributable files](#microsoft-redistributable-files) (the Visual C++
  runtime, `vc_redist.x64.exe`, `d3dcompiler_47.dll`, `dxcompiler.dll`,
  `dxil.dll`).
- The installer and the uninstaller are built with [NSIS](#nsis).

### macOS

`freetunnel-macos-universal.dmg` holds `FreeTunnel.app`:

- FreeTunnel. Compiled into it: [the VPN core and its
  libraries](#the-vpn-core-and-its-libraries), [QHotkey](#qhotkey), [the
  Feather and Lucide icons](#feather-and-lucide-icons) and the [Inter](#inter)
  typeface.
- In `Contents/Frameworks` and `Contents/PlugIns`: [Qt 6.11.2](#qt) with [the
  third-party code inside Qt](#third-party-code-inside-qt).

### Linux

`freetunnel-linux-x86_64.deb` (installed under `/opt/freetunnel`) and
`freetunnel-x86_64.AppImage` hold the same files:

- FreeTunnel. Compiled into it: [the VPN core and its
  libraries](#the-vpn-core-and-its-libraries), [QHotkey](#qhotkey), [the
  Feather and Lucide icons](#feather-and-lucide-icons) and the [Inter](#inter)
  typeface.
- In `lib/` and `plugins/`: [Qt 6.11.2](#qt) with [the third-party code inside
  Qt](#third-party-code-inside-qt), [ICU 73](#icu), [Ubuntu 20.04 system
  libraries](#ubuntu-2004-system-libraries) and [the GCC runtime
  libraries](#gcc-runtime-libraries).
- The AppImage only: [the AppImage runtime](#appimage-runtime) at the start of
  the file.

### Android

`freetunnel-android-<version>.apk` contains:

- FreeTunnel for Android.
- [The TrustTunnel Android client](#trusttunnel-android-client) 1.1.7, whose
  native library contains [the VPN core and its
  libraries](#the-vpn-core-and-its-libraries) and [Rust
  crates](#rust-crates-in-the-trusttunnel-native-library).
- [Java and Kotlin libraries](#java-and-kotlin-libraries).
- [Icons derived from Feather and Lucide](#feather-and-lucide-icons-in-the-android-app).
- [The Inter typeface](#inter).

## The VPN core and its libraries

Every download contains the TrustTunnel VPN core. The desktop builds compile it
from source and link it statically into FreeTunnel. In the Android app it is
part of the TrustTunnel Android client's native library
(`libtrusttunnel_android.so`), built by the TrustTunnel project from the same
release. Unless noted, each library below is compiled into the core in all
downloads.

### TrustTunnel client core

- Version: TrustTunnelClient 1.1.7, commit
  170609c24ca865819fed68437b01c013049bc3fa. The desktop builds apply
  FreeTunnel's changes to it, which are in
  [vendor/trusttunnel/](vendor/trusttunnel/) in FreeTunnel's source.
- License: Apache-2.0
- Copyright: Copyright 2020 Adguard Software Ltd
- Source: <https://github.com/TrustTunnel/TrustTunnelClient/tree/170609c24ca865819fed68437b01c013049bc3fa>

### NativeLibsCommon

- Version: 8.1.52, commit 58cef252031e2cc1f540ecaec2952f5f32afa3a1
- License: Apache-2.0
- Copyright: Copyright 2022 Adguard Software Ltd
- Source: <https://github.com/AdguardTeam/NativeLibsCommon/tree/58cef252031e2cc1f540ecaec2952f5f32afa3a1>

### DnsLibs

- Version: 2.10.2, commit 0c6e855b12eee2f696e7cc30719532fda4fdd512
- License: Apache-2.0
- Copyright: Copyright 2022 Adguard Software Ltd
- Source: <https://github.com/AdguardTeam/DnsLibs/tree/0c6e855b12eee2f696e7cc30719532fda4fdd512>

### BoringSSL

The VPN core's TLS and cryptography library, which the desktop app's update
check also uses to verify release signatures. It is BoringSSL, not OpenSSL, and
no download ships an OpenSSL library. (Qt's OpenSSL TLS plugin in the Linux
packages uses the system's OpenSSL, if one is installed.)

- Version: commit d589045a772678d5ca131f4c8087d001b9258380 (tag 0.20260508.0),
  modified by the patches of AdGuard's conan recipe
  (<https://github.com/AdguardTeam/NativeLibsCommon/tree/58cef252031e2cc1f540ecaec2952f5f32afa3a1/conan/recipes/boringssl>)
- License: Apache-2.0
- Copyright: Copyright The BoringSSL Authors; Copyright 1995-2016 The OpenSSL
  Project Authors; Copyright (c) 2002, Oracle and/or its affiliates; Copyright
  2005 Nokia; Copyright 2015-2020 the fiat-crypto authors (`third_party/fiat`)
- Source: <https://boringssl.googlesource.com/boringssl/+/d589045a772678d5ca131f4c8087d001b9258380>

### nghttp2

- Version: 1.56.0, with the patches of AdGuard's conan recipe
  (<https://github.com/AdguardTeam/NativeLibsCommon/tree/58cef252031e2cc1f540ecaec2952f5f32afa3a1/conan/recipes/nghttp2-1.56.0>)
- License: MIT, [licenses/common/nghttp2.txt](licenses/common/nghttp2.txt)
- Copyright: Copyright (c) 2012, 2014, 2015, 2016 Tatsuhiro Tsujikawa;
  Copyright (c) 2012, 2014, 2015, 2016 nghttp2 contributors
- Source: <https://github.com/nghttp2/nghttp2/tree/v1.56.0>

### nghttp3

- Version: 1.0.0, with the patches of AdGuard's conan recipe
  (<https://github.com/AdguardTeam/NativeLibsCommon/tree/58cef252031e2cc1f540ecaec2952f5f32afa3a1/conan/recipes/nghttp3-1.0.0>)
- License: MIT, [licenses/common/nghttp3.txt](licenses/common/nghttp3.txt)
- Copyright: Copyright (c) 2019 nghttp3 contributors
- Source: <https://github.com/ngtcp2/nghttp3/tree/v1.0.0>

### ngtcp2

- Version: 1.22.1, with the patches of AdGuard's conan recipe
  (<https://github.com/AdguardTeam/NativeLibsCommon/tree/58cef252031e2cc1f540ecaec2952f5f32afa3a1/conan/recipes/ngtcp2-1.22.1>)
- License: MIT, [licenses/common/ngtcp2.txt](licenses/common/ngtcp2.txt)
- Copyright: Copyright (c) 2016 ngtcp2 contributors
- Source: <https://github.com/ngtcp2/ngtcp2/tree/v1.22.1>

### libevent

- Version: 2.1.11-stable, with the patches of AdGuard's conan recipe
  (<https://github.com/AdguardTeam/NativeLibsCommon/tree/58cef252031e2cc1f540ecaec2952f5f32afa3a1/conan/recipes/libevent>)
- License: BSD-3-Clause AND ISC AND MIT,
  [licenses/common/libevent.txt](licenses/common/libevent.txt)
- Copyright: Copyright (c) 2000-2007 Niels Provos; Copyright (c) 2007-2012
  Niels Provos and Nick Mathewson; and the other holders listed in its license
- Source: <https://github.com/libevent/libevent/tree/release-2.1.11-stable>

### ldns

- Version: 2021-03-29, commit 7128ef56649e0737f236bc5d5d640de38ff0036d, as
  AdGuard's conan recipe builds it
- License: BSD-3-Clause AND ISC, [licenses/common/ldns.txt](licenses/common/ldns.txt)
- Copyright: Copyright (c) 2005,2006, NLnetLabs; Copyright (c) 2001-2008, NLnet
  Labs; Copyright (c) 2000-2001, Aaron D. Gifford; Copyright (c) 1996, 1998 by
  Internet Software Consortium; Portions Copyright (c) 1995 by International
  Business Machines, Inc.; Copyright (c) 1998 Todd C. Miller
- Source: <https://github.com/NLnetLabs/ldns/tree/7128ef56649e0737f236bc5d5d640de38ff0036d>

### klib

- Version: 2021-04-06, commit e1b2a40de8e2a46c05cc5dac9c6e5e8d15ae722c
  (`khash.h` and `kvec.h`)
- License: MIT, [licenses/common/klib.txt](licenses/common/klib.txt)
- Copyright: Copyright (c) 2008, 2009, 2011 by Attractive Chaos
- Source: <https://github.com/attractivechaos/klib/tree/e1b2a40de8e2a46c05cc5dac9c6e5e8d15ae722c>

### Brotli

- Version: 1.1.0
- License: MIT, [licenses/common/brotli.txt](licenses/common/brotli.txt)
- Copyright: Copyright (c) 2009, 2010, 2013-2016 by the Brotli Authors
- Source: <https://github.com/google/brotli/tree/v1.1.0>

### zlib

- Version: 1.3.1
- License: Zlib, [licenses/common/zlib.txt](licenses/common/zlib.txt)
- Copyright: Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler
- Source: <https://github.com/madler/zlib/tree/v1.3.1>

### http-parser

- Version: 2.9.4
- License: MIT, [licenses/common/http-parser.txt](licenses/common/http-parser.txt)
- Copyright: Copyright Joyent, Inc. and other Node contributors
- Source: <https://github.com/nodejs/http-parser/tree/v2.9.4>

### llhttp

- Version: 9.3.0, with the patches of AdGuard's conan recipe
  (<https://github.com/AdguardTeam/NativeLibsCommon/tree/58cef252031e2cc1f540ecaec2952f5f32afa3a1/conan/recipes/llhttp-9.3.0>)
- License: MIT, [licenses/common/llhttp.txt](licenses/common/llhttp.txt)
- Copyright: Copyright Fedor Indutny, 2018
- Source: <https://github.com/nodejs/llhttp/tree/release/v9.3.0>

### PCRE2

- Version: 10.37 (the 8-bit library, built without its JIT compiler)
- License: BSD-3-Clause, [licenses/common/pcre2-10.37.txt](licenses/common/pcre2-10.37.txt)
- Copyright: Copyright (c) 1997-2021 University of Cambridge
- Source: <https://github.com/PCRE2Project/pcre2/releases/tag/pcre2-10.37>

### libsodium

- Version: 1.0.18
- License: ISC, [licenses/common/libsodium.txt](licenses/common/libsodium.txt)
- Copyright: Copyright (c) 2013-2019 Frank Denis
- Source: <https://github.com/jedisct1/libsodium/tree/1.0.18>

### libuv

- Version: 1.41.0, with the patches of AdGuard's conan recipe
  (<https://github.com/AdguardTeam/NativeLibsCommon/tree/58cef252031e2cc1f540ecaec2952f5f32afa3a1/conan/recipes/libuv>)
- License: MIT AND BSD-2-Clause AND BSD-3-Clause AND ISC,
  [licenses/common/libuv.txt](licenses/common/libuv.txt)
- Copyright: Copyright (c) 2015-present libuv project contributors; Copyright
  Joyent, Inc. and other Node contributors; and the holders of its bundled
  parts, listed in its license
- Source: <https://github.com/libuv/libuv/tree/v1.41.0>

### tldregistry

AdGuard's library for looking up public suffixes. It compiles Chromium's lookup
code and a table built from the Public Suffix List.

- Version: 2022-12-26, in NativeLibsCommon 8.1.52
- License: BSD-3-Clause (Chromium's code,
  [licenses/common/chromium-tldregistry.txt](licenses/common/chromium-tldregistry.txt))
  AND MPL-2.0 (the Public Suffix List, [licenses/common/MPL-2.0.txt](licenses/common/MPL-2.0.txt))
- Copyright: Copyright 2015 The Chromium Authors; the Public Suffix List is
  maintained by its contributors at publicsuffix.org
- Source: <https://github.com/AdguardTeam/NativeLibsCommon/tree/58cef252031e2cc1f540ecaec2952f5f32afa3a1/conan/recipes/tldregistry>;
  the list's Source Code Form: <https://publicsuffix.org/list/>,
  <https://github.com/publicsuffix/list>

### ada

- Version: 2.7.4
- License: Apache-2.0 OR MIT, used under Apache-2.0
- Copyright: Copyright 2023 Yagiz Nizipli and Daniel Lemire
- Source: <https://github.com/ada-url/ada/tree/v2.7.4>

### fmt

- Version: 12.1.0
- License: MIT, [licenses/common/fmt.txt](licenses/common/fmt.txt)
- Copyright: Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
- Source: <https://github.com/fmtlib/fmt/tree/12.1.0>

### magic_enum

- Version: 0.9.5
- License: MIT, [licenses/common/magic_enum.txt](licenses/common/magic_enum.txt)
- Copyright: Copyright (c) 2019 - 2023 Daniil Goncharov
- Source: <https://github.com/Neargye/magic_enum/tree/v0.9.5>

### JSON for Modern C++

- Version: 3.12.0
- License: MIT, [licenses/common/nlohmann-json.txt](licenses/common/nlohmann-json.txt)
- Copyright: Copyright (c) 2013-2025 Niels Lohmann
- Source: <https://github.com/nlohmann/json/tree/v3.12.0>

### toml++

- Version: 3.3.0
- License: MIT, [licenses/common/tomlplusplus.txt](licenses/common/tomlplusplus.txt)
- Copyright: Copyright (c) Mark Gillard
- Source: <https://github.com/marzer/tomlplusplus/tree/v3.3.0>

### cxxopts

- Version: 3.1.1
- License: MIT, [licenses/common/cxxopts.txt](licenses/common/cxxopts.txt)
- Copyright: Copyright (c) 2014 Jarryd Beck
- Source: <https://github.com/jarro2783/cxxopts/tree/v3.1.1>

### lwIP

- Version: 2.1.2, as vendored in TrustTunnelClient
- License: BSD-3-Clause, [licenses/common/lwip.txt](licenses/common/lwip.txt)
- Copyright: Copyright (c) 2001, 2002 Swedish Institute of Computer Science;
  and the other holders listed in [licenses/common/lwip.txt](licenses/common/lwip.txt)
- Source: <https://github.com/TrustTunnel/TrustTunnelClient/tree/170609c24ca865819fed68437b01c013049bc3fa/third-party/lwip/lwip>

### pcap_savefile.h

A header from libpcap that the core uses to write packet captures.

- Version: as vendored in TrustTunnelClient at 170609c2
- License: BSD-4-Clause-UC, [licenses/common/pcap_savefile.txt](licenses/common/pcap_savefile.txt).
  This product includes software developed by the Computer Systems Engineering
  Group at Lawrence Berkeley Laboratory.
- Copyright: Copyright (c) 1994, 1995, 1996 The Regents of the University of California
- Source: <https://github.com/TrustTunnel/TrustTunnelClient/blob/170609c24ca865819fed68437b01c013049bc3fa/third-party/pcap_savefile/include/pcap_savefile.h>

### Microsoft Detours

Windows only.

- Version: 2021-04-14, commit fe7216c037c898b1f65330eda85e6146b6e3cb85
- License: MIT, [licenses/desktop/Detours.txt](licenses/desktop/Detours.txt)
- Copyright: Copyright (c) Microsoft Corporation
- Source: <https://github.com/microsoft/Detours/tree/fe7216c037c898b1f65330eda85e6146b6e3cb85>

## The typeface

### Inter

The text of FreeTunnel's windows and screens is set in Inter, in the desktop
apps and in the Android app: its Regular and Medium weights, as the hinted
static TrueType files of the official release, unmodified. The desktop builds
compile them into FreeTunnel; the Android app carries them as font resources.
The exceptions are text the operating system draws itself (the tray menu, file
dialogs, notifications) and the log and configuration text, which are set in
the system's monospace font. The files and their checksums are in
[assets/fonts/inter/](assets/fonts/inter/), with the release they come from in
`SOURCE`. The Android app also draws four characters of its screens (✓ ↓ ↑ ←)
as vector drawables, `res/drawable/ic_check.xml`, `ic_arrow_down.xml`,
`ic_arrow_up.xml` and `ic_back.xml`, which `scripts/android-icons-from-svg.py`
makes from the outlines of those glyphs in `Inter-Regular.ttf`, so that they are
the desktop's shapes; they are covered by the same license.

- Version: Inter 4.1 (tag v4.1, commit e3a3d4c57d5ecc01453a575621882a384c1995a3):
  `Inter-Regular.ttf` and `Inter-Medium.ttf` from `Inter-4.1.zip`
- License: OFL-1.1, the SIL Open Font License 1.1,
  [licenses/common/Inter-OFL-1.1.txt](licenses/common/Inter-OFL-1.1.txt). It
  names no Reserved Font Name.
- Copyright: Copyright (c) 2016 The Inter Project Authors
  (<https://github.com/rsms/inter>)
- Source: <https://github.com/rsms/inter/tree/v4.1>

## Desktop components

### Qt

FreeTunnel's desktop interface is built with Qt 6.11.2: The Qt Company's
official binaries, built from unmodified source. On Windows, windeployqt
copies Qt's DLLs and plugins unchanged. On macOS, macdeployqt rewrites the
install names that say where Qt's frameworks are and strips their debugging
symbols, and the bundle's ad-hoc signature is added to each of them. On Linux,
linuxdeploy sets the RPATH of Qt's libraries and plugins, the path they load
libraries from, and does not strip them. FreeTunnel uses Qt under the GNU
Lesser General Public License version 3
([licenses/desktop/LGPL-3.0.txt](licenses/desktop/LGPL-3.0.txt)),
which incorporates the GNU General Public License version 3
([licenses/desktop/GPL-3.0.txt](licenses/desktop/GPL-3.0.txt)). Qt is
also available under other licenses: `LicenseRef-Qt-Commercial OR
LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`.

- Version: Qt 6.11.2: the libraries, plugins and QML modules of qtbase,
  qtdeclarative and qtsvg that FreeTunnel needs, and Qt's Russian translations
  from qttranslations
- License: LGPL-3.0-only
- Copyright: Copyright (C) The Qt Company Ltd. and other contributors
- Source: the complete source of Qt 6.11.2 is at
  <https://download.qt.io/archive/qt/6.11/6.11.2/single/qt-everywhere-src-6.11.2.tar.xz>,
  and module by module (`qtbase-everywhere-src-6.11.2.tar.xz` and so on) at
  <https://download.qt.io/archive/qt/6.11/6.11.2/submodules/>

FreeTunnel links Qt dynamically, so you can replace the Qt libraries with your
own build of Qt 6.11.2, modified or not:

- Windows: the `Qt6*.dll` files and the plugin and `qml` folders next to
  `FreeTunnel.exe` (Qt built with MSVC 2022 for x64).
- macOS: the frameworks in `FreeTunnel.app/Contents/Frameworks` and the plugins
  in `Contents/PlugIns`. The bundle is signed ad hoc; after replacing a
  library, sign it again with `codesign --force --deep --sign - FreeTunnel.app`.
- Linux: the `libQt6*.so.6` files in `/opt/freetunnel/lib` (.deb). For the
  AppImage, unpack it with `--appimage-extract`, replace them in
  `squashfs-root/usr/lib` and start `squashfs-root/AppRun`.

Nothing in FreeTunnel's license restricts modifying the Qt libraries in it, or
reverse engineering FreeTunnel to debug such modifications.

### Third-party code inside Qt

Qt contains code from other projects, compiled into its libraries and plugins.
[licenses/desktop/Qt-6.11.2-third-party.txt](licenses/desktop/Qt-6.11.2-third-party.txt)
lists each one with its version, license, copyright and license text, as Qt's
own attribution files at tag v6.11.2 give them: among them FreeType, HarfBuzz,
libpng, libjpeg-turbo, PCRE2, zlib, double-conversion, TinyCBOR, MD4C, the
Unicode Character Database and CLDR, the Public Suffix List, the
JavaScriptCore macro assembler and XSVG.

Portions of this software are copyright © 2026 The FreeType Project
([www.freetype.org](https://www.freetype.org)). All rights reserved.

### QHotkey

- Version: 1.5.0, commit 34330d6ff5d2ca111c376f6d7da66be9d1817430
- License: BSD-3-Clause, [licenses/desktop/QHotkey.txt](licenses/desktop/QHotkey.txt)
- Copyright: Copyright (c) 2016, Felix Barz
- Source: <https://github.com/Skycoder42/QHotkey/tree/34330d6ff5d2ca111c376f6d7da66be9d1817430>

### QWindowKit

Windows only: the frameless window's shadow, corners and snap layouts.

- Version: 1.5.0, commit 35e88f3655720ed0537c7dd1dde243bf4a70c94c (the Qt
  Quick module, linked statically)
- License: Apache-2.0
- Copyright: Copyright (C) 2023-2025 Stdware Collections; Copyright (C)
  2021-2023 wangwenx190 (Yuhang Zhao)
- Source: <https://github.com/stdware/qwindowkit/tree/35e88f3655720ed0537c7dd1dde243bf4a70c94c>

### Feather and Lucide icons

Five of the interface's icons (`assets/icons/settings.svg`, `export.svg`,
`close.svg` and `connection.svg` from Feather, `edit.svg` from Lucide) are
derived from these icon sets. The Android app draws the same icons: see
[Feather and Lucide icons in the Android
app](#feather-and-lucide-icons-in-the-android-app).

- License: MIT (Feather) AND ISC (Lucide),
  [licenses/common/Feather-Lucide-icons.txt](licenses/common/Feather-Lucide-icons.txt)
- Copyright: Copyright (c) 2013-2023 Cole Bemis; Copyright (c) Lucide Icons and
  Contributors
- Source: <https://github.com/feathericons/feather>, <https://github.com/lucide-icons/lucide>

### Wintun

Windows only. The VPN core creates its network adapter with `wintun.dll`,
which FreeTunnel ships unmodified from the official release and uses only
through the API of `wintun.h`.

- Version: Wintun 0.14.1 (`wintun-0.14.1.zip`)
- License: `wintun.dll`: the Wintun Prebuilt Binaries License; `wintun.h`,
  compiled into the core: GPL-2.0 OR MIT, used under MIT. Both are in
  [licenses/desktop/Wintun.txt](licenses/desktop/Wintun.txt).
- Copyright: Copyright (C) 2018-2021 WireGuard LLC. All Rights Reserved.
- Source: <https://www.wintun.net/builds/wintun-0.14.1.zip>, <https://git.zx2c4.com/wintun>

### Mesa llvmpipe

Windows only: `opengl32sw.dll`, a software OpenGL renderer that comes with
Qt's Windows binaries, for systems whose own OpenGL Qt cannot use.

- Version: as shipped with Qt 6.11.2 for Windows
- License: MIT AND BSL-1.0, [licenses/desktop/Mesa-llvmpipe.txt](licenses/desktop/Mesa-llvmpipe.txt)
- Copyright: Copyright (C) 1999-2007 Brian Paul; Copyright (c) 2013-2016 The
  Khronos Group Inc.; (C) Copyright yohhoy 2012
- Source: <https://gitlab.freedesktop.org/mesa/mesa>

### Microsoft redistributable files

Windows only. These are Microsoft Corporation's files, redistributed unmodified
under Microsoft's terms for them, which
[licenses/desktop/Microsoft-redistributables.txt](licenses/desktop/Microsoft-redistributables.txt)
describes:

- the Visual C++ runtime DLLs (`msvcp140.dll`, `vcruntime140.dll` and the rest
  of the Visual Studio `Microsoft.VC14x.CRT` folder) and the Visual C++
  Redistributable installer `vc_redist.x64.exe`;
- the Direct3D shader compilers `d3dcompiler_47.dll`, `dxcompiler.dll` and
  `dxil.dll`, from the Windows SDK.

### NSIS

The setup program and the uninstaller contain the runtime of NSIS, the
installer system they are built with.

- Version: NSIS 3.13.0
- License: Zlib AND CPL-1.0 (its LZMA decompressor, under the Common Public
  License 1.0 with the authors' linking exception),
  [licenses/desktop/NSIS.txt](licenses/desktop/NSIS.txt). The setup program and
  the uninstaller are compressed with LZMA, so both contain the LZMA module in
  object code, which FreeTunnel distributes under the Common Public License
  1.0. Its source code is available at the address below, in `Source/7zip/`.
  On behalf of the LZMA module's contributors, FreeTunnel disclaims all
  warranties and conditions, express and implied, including those of title,
  non-infringement, merchantability and fitness for a particular purpose, and
  excludes their liability for all damages, direct, indirect, special,
  incidental and consequential, including lost profits. Any terms here that
  differ from the CPL are offered by FreeTunnel alone.
- Copyright: Copyright (C) 1999-2026 Contributors; the LZMA module: Igor Pavlov
  and Amir Szekely
- Source: <https://github.com/nsis-dev/nsis/tree/v313>, <https://nsis.sourceforge.io/>

### ICU

Linux only: `libicudata.so.73`, `libicui18n.so.73` and `libicuuc.so.73`, which
Qt's Linux binaries come with.

- Version: 73
- License: Unicode-DFS-2016 AND ICU, with the licenses of the third-party code
  ICU contains, [licenses/desktop/ICU-73.txt](licenses/desktop/ICU-73.txt)
- Copyright: Copyright © 1991-2023 Unicode, Inc.; Copyright (C) 2016 and later:
  Unicode, Inc. and others
- Source: <https://github.com/unicode-org/icu/tree/release-73-2/icu4c>

### Ubuntu 20.04 system libraries

Linux only. The Linux packages are built on Ubuntu 20.04, and they carry the
system libraries Qt and FreeTunnel need that are not part of every Linux
system, from Ubuntu's packages (linuxdeploy strips their debugging symbols and
sets the paths they load libraries from). `bundled-system-packages.txt`, next
to this file in the Linux packages, lists each package with its exact version
and source package; it is written when the package is built. Each package's
Debian copyright file, where Ubuntu's package has one, is shipped with it
(see [Where to find these notices](#where-to-find-these-notices)). At the time
of writing, the libraries come from these packages:

| Ubuntu packages | License |
| --- | --- |
| libglib2.0-0 (GLib, GIO, GObject, GModule, GThread) | LGPL-2.1-or-later |
| libsecret-1-0 | LGPL-2.1-or-later |
| libsystemd0 | LGPL-2.1-or-later |
| libgcrypt20 | LGPL-2.1-or-later |
| libmount1, libblkid1 (util-linux) | LGPL-2.1-or-later |
| libkeyutils1 | LGPL-2.0-or-later |
| libdbus-1-3 | AFL-2.1 OR GPL-2.0-or-later, used under AFL-2.1 |
| libselinux1 | Public domain |
| libpcre3 | BSD-3-Clause |
| libpcre2-8-0 | BSD-3-Clause |
| liblzma5 | Public domain |
| liblz4-1 | BSD-2-Clause |
| libzstd1 | BSD-3-Clause OR GPL-2.0-only, used under BSD-3-Clause |
| libffi7 | MIT |
| libpng16-16 | Libpng AND libpng-2.0 |
| libbsd0 | BSD-3-Clause AND BSD-2-Clause AND ISC AND MIT, and others in its copyright file |
| libgssapi-krb5-2, libkrb5-3, libk5crypto3, libkrb5support0 (MIT Kerberos) | MIT Kerberos's MIT-style licenses |
| libxcb-cursor0, libxcb-glx0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render0, libxcb-render-util0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-util1, libxcb-xfixes0, libxcb-xkb1 | MIT |
| libxau6, libxdmcp6 | MIT |
| libxkbcommon0, libxkbcommon-x11-0 | MIT |

The texts: LGPL-2.0-or-later and LGPL-2.1-or-later libraries,
[licenses/desktop/LGPL-2.1.txt](licenses/desktop/LGPL-2.1.txt); libdbus,
[licenses/desktop/AFL-2.1.txt](licenses/desktop/AFL-2.1.txt); the others'
notices are in their Debian copyright files.

### GCC runtime libraries

Linux only: `libstdc++.so.6` and `libgcc_s.so.1`, from the packages libstdc++6
and libgcc-s1 of Ubuntu's `ppa:ubuntu-toolchain-r/test`. The build installs
GCC 13 from that PPA, which brings in the newest libstdc++6 and libgcc-s1 the
PPA has, so these libraries can come from a later GCC than 13. At the time of
writing they are a GCC 16 snapshot: source package `gcc-16`, version
`16-20260315-1ubuntu1~20~ppa1`. `bundled-system-packages.txt` gives the exact
version in each package.

- License: GPL-3.0-or-later WITH GCC-exception-3.1,
  [licenses/desktop/GPL-3.0.txt](licenses/desktop/GPL-3.0.txt) and
  [licenses/desktop/GCC-exception-3.1.txt](licenses/desktop/GCC-exception-3.1.txt)
- Copyright: Copyright (C) Free Software Foundation, Inc.
- Source: `gcc-runtime-source.tar`, attached to the same release as the Linux
  packages on <https://github.com/dimmmmmmmer/freetunnel/releases>. It holds
  the source package of exactly the version they carry, as the PPA published
  it: unpack it with `tar -xf` and then `dpkg-source -x` on its `.dsc` file.
  The PPA itself keeps only its newest version.

### AppImage runtime

The AppImage only: the small program at the start of the `.AppImage` file that
mounts or extracts the image. It is the type 2 runtime of the AppImage project
and links libfuse, squashfuse, zstd, zlib, musl and mimalloc statically.

- Version: the `continuous` build of AppImage/type2-runtime at the time the
  AppImage was built. `appimage-runtime.txt`, next to this file in the
  AppImage, names the commit it was built from and its SHA-256. At the time of
  writing that is commit fedfad51acc2860b2916a1eef32ba4a500ff31ef, which links
  libfuse 3.15.0, squashfuse 0.5.2, and musl 1.2.5, zstd 1.5.6, zlib and
  mimalloc from Alpine Linux 3.21.
- License: MIT AND LGPL-2.1-only (libfuse) AND BSD-2-Clause (squashfuse) AND
  BSD-3-Clause (zstd) AND Zlib AND MIT (musl, mimalloc),
  [licenses/desktop/AppImage-runtime.txt](licenses/desktop/AppImage-runtime.txt),
  with libfuse's license text in [licenses/desktop/LGPL-2.1.txt](licenses/desktop/LGPL-2.1.txt)
- Copyright: Copyright (c) 2004-23 probonopd; libfuse: Copyright (C) 2001-2007
  Miklos Szeredi and contributors; squashfuse: Dave Vasilevsky
- Source: `https://github.com/AppImage/type2-runtime/tree/<commit>`, for the
  commit in `appimage-runtime.txt`. Its `scripts/common/install-dependencies.sh`
  names the libfuse release the runtime is built with, and its
  `patches/libfuse/` the changes made to it; at the time of writing libfuse
  3.15.0, <https://github.com/libfuse/libfuse/releases/tag/fuse-3.15.0>, with
  `patches/libfuse/mount.c.diff`.

## Android components

### TrustTunnel Android client

The VPN client library the app is built on (`trusttunnel-client-android-1.1.7.aar`):
its Java and Kotlin classes, its `assets/logback.xml`, and its native library,
which contains the VPN core and its libraries listed above (all but Detours and
wintun.h), the JNI adapter, and the Rust code below. The native library is
built with the Android NDK's LLVM libc++ (Apache-2.0 WITH LLVM-exception),
which asks for no notice in compiled form.

- Version: 1.1.7 (TrustTunnelClient commit 170609c24ca865819fed68437b01c013049bc3fa)
- License: Apache-2.0
- Copyright: Copyright 2020 Adguard Software Ltd
- Source: <https://github.com/TrustTunnel/TrustTunnelClient/tree/v1.1.7/platform/android>

### Rust crates in the TrustTunnel native library

Compiled into `libtrusttunnel_android.so` with the Rust standard library, as
TrustTunnelClient's `trusttunnel/Cargo.lock` at 170609c2 resolves them.

- trusttunnel-deeplink-ffi 0.1.0 and trusttunnel-settings 0.1.0 (in
  TrustTunnelClient), Apache-2.0, Copyright 2020 Adguard Software Ltd
- trusttunnel-deeplink 0.1.0 (TrustTunnel commit
  c33f2a6a3a9490058ca76a4274f4c96da9ec51e6), Apache-2.0, Copyright 2020
  Adguard Software Ltd, <https://github.com/TrustTunnel/TrustTunnel/tree/c33f2a6a3a9490058ca76a4274f4c96da9ec51e6/deeplink>
- MIT OR Apache-2.0 (rustls-pemfile: Apache-2.0 OR ISC OR MIT), each used
  under Apache-2.0, sources on <https://crates.io>:
  - base64 0.21.7: Copyright (c) 2015 Alice Maz
  - hex 0.4.3: Copyright (c) 2013-2014 The Rust Project Developers; Copyright
    (c) 2015-2020 The rust-hex Developers
  - rustls-pemfile 1.0.4: Copyright (c) 2016 Joseph Birr-Pixton
  - thiserror 1.0.69, serde 1.0.228, serde_core 1.0.228: their authors (the
    license files name no holder)
  - toml 0.7.8, toml_edit 0.19.15, toml_datetime 0.6.11, serde_spanned 0.6.9:
    Copyright (c) Individual contributors
  - indexmap 2.13.0, equivalent 1.0.2: their authors (the license files name
    no holder)
  - hashbrown 0.16.1: Copyright (c) 2016 Amanieu d'Antras
- winnow 0.5.40, MIT, [licenses/android/winnow.txt](licenses/android/winnow.txt),
  <https://github.com/winnow-rs/winnow/tree/v0.5.40>
- memchr 2.8.0 (Andrew Gallant), Unlicense OR MIT, used under the Unlicense,
  <https://github.com/BurntSushi/memchr/tree/2.8.0>
- the Rust standard library, MIT OR Apache-2.0, used under Apache-2.0,
  Copyright (c) The Rust Project Contributors, <https://github.com/rust-lang/rust>

### Java and Kotlin libraries

Compiled into the app. The ones the app's build names are given with their
Maven coordinates; the others come in as their dependencies.

#### SLF4J API

- Version: 1.7.25 (`org.slf4j:slf4j-api:1.7.25`)
- License: MIT, [licenses/android/slf4j.txt](licenses/android/slf4j.txt)
- Copyright: Copyright (c) 2004-2017 QOS.ch
- Source: <https://github.com/qos-ch/slf4j/tree/v_1.7.25>

#### logback-android

- Version: 2.0.0 (`com.github.tony19:logback-android:2.0.0`)
- License: Apache-2.0, as published. It is derived from QOS.ch's logback,
  which is EPL-1.0 OR LGPL-2.1-only. To the extent that logback's code in it
  remains under those terms, FreeTunnel distributes it under the Eclipse
  Public License 1.0, [licenses/android/EPL-1.0.txt](licenses/android/EPL-1.0.txt),
  and its source code is available at the address below. On behalf of
  logback's contributors, FreeTunnel disclaims all warranties and conditions,
  express and implied, including those of title, non-infringement,
  merchantability and fitness for a particular purpose, and excludes their
  liability for all damages, direct, indirect, special, incidental and
  consequential, including lost profits. Any terms here that differ from the
  EPL are offered by FreeTunnel alone.
- Copyright: Copyright 2019 Anthony Trinh; Copyright (C) 2011-2012, Anthony
  Trinh; Copyright (C) 1999-2011, QOS.ch
- Source: <https://github.com/tony19/logback-android/tree/v_2.0.0>

#### RxAndroid and RxJava

- Version: RxAndroid 3.0.0 (`io.reactivex.rxjava3:rxandroid:3.0.0`), with
  RxJava 3.0.0 and Reactive Streams 1.0.3
- License: Apache-2.0 (RxAndroid, RxJava); Reactive Streams is CC0-1.0 (public
  domain dedication)
- Copyright: RxJava: Copyright (c) 2016-present, RxJava Contributors; RxAndroid:
  the RxAndroid authors
- Source: <https://github.com/ReactiveX/RxAndroid/tree/3.0.0>,
  <https://github.com/ReactiveX/RxJava/tree/v3.0.0>,
  <https://github.com/reactive-streams/reactive-streams-jvm/tree/v1.0.3>

#### ktoml

- Version: 0.7.0 (`com.akuleshov7:ktoml-core:0.7.0`)
- License: MIT, [licenses/android/ktoml.txt](licenses/android/ktoml.txt)
- Copyright: Copyright (c) 2023 Andrey Kuleshov
- Source: <https://github.com/akuleshov7/ktoml/tree/v0.7.0>

#### kotlinx.serialization and kotlinx-datetime

ktoml's dependencies.

- Version: kotlinx-serialization-core 1.8.1, kotlinx-datetime 0.6.2
- License: Apache-2.0, with kotlinx.serialization's NOTICE
  ([licenses/android/NOTICE-files.txt](licenses/android/NOTICE-files.txt));
  kotlinx-datetime's date calculations derive from the ThreeTen backport,
  BSD-3-Clause ([licenses/android/ThreeTen-Backport.txt](licenses/android/ThreeTen-Backport.txt))
- Copyright: Copyright 2017-2019 JetBrains s.r.o and respective authors and
  developers; JetBrains s.r.o. and contributors; ThreeTen backport: Copyright
  (c) 2007-present, Stephen Colebourne & Michael Nascimento Santos
- Source: <https://github.com/Kotlin/kotlinx.serialization/tree/v1.8.1>,
  <https://github.com/Kotlin/kotlinx-datetime/tree/v0.6.2>

#### Tomlj

- Version: 1.1.1 (`org.tomlj:tomlj:1.1.1`)
- License: Apache-2.0. Its source files carry the Apache Software Foundation's
  license header; the project has no NOTICE file.
- Copyright: the Tomlj authors
- Source: <https://github.com/tomlj/tomlj/tree/1.1.1>

#### ANTLR 4 runtime

A dependency of Tomlj.

- Version: 4.11.1
- License: BSD-3-Clause, [licenses/android/ANTLR-4.txt](licenses/android/ANTLR-4.txt)
- Copyright: Copyright (c) 2012-2022 The ANTLR Project
- Source: <https://github.com/antlr/antlr4/tree/4.11.1>

#### Checker Framework qualifiers

A dependency of Tomlj.

- Version: checker-qual 3.21.2
- License: MIT, [licenses/android/checker-qual.txt](licenses/android/checker-qual.txt)
- Copyright: Copyright 2004-present by the Checker Framework developers
- Source: <https://github.com/typetools/checker-framework/tree/checker-framework-3.21.2/checker-qual>

#### Bouncy Castle

- Version: 1.86 (`org.bouncycastle:bcprov-jdk15to18:1.86`)
- License: MIT, [licenses/android/BouncyCastle.txt](licenses/android/BouncyCastle.txt)
- Copyright: Copyright (c) 2000-2026 The Legion of the Bouncy Castle Inc.
- Source: <https://github.com/bcgit/bc-java/tree/r1rv86>

#### QR Code generator

- Version: 1.8.0 (`io.nayuki:qrcodegen:1.8.0`)
- License: MIT, [licenses/android/qrcodegen.txt](licenses/android/qrcodegen.txt)
- Copyright: Copyright (c) Project Nayuki
- Source: <https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/java>

#### AndroidX

- Version: Core KTX 1.16.0 (`androidx.core:core-ktx:1.16.0`), with the
  AndroidX libraries it depends on (Core, Core ViewTree, Annotation, Annotation
  Experimental, Collection, Concurrent Futures, Interpolator, Lifecycle, Arch
  Core, ProfileInstaller, Startup, Tracing, VersionedParcelable)
- License: Apache-2.0
- Copyright: Copyright The Android Open Source Project
- Source: <https://android.googlesource.com/platform/frameworks/support/>

#### Kotlin standard library and kotlinx.coroutines

- Version: kotlin-stdlib 2.1.20, kotlinx-coroutines-core and
  kotlinx-coroutines-android 1.6.4 (a dependency of AndroidX Lifecycle),
  JetBrains annotations 13.0
- License: Apache-2.0, with the NOTICE files of Kotlin and kotlinx.coroutines
  ([licenses/android/NOTICE-files.txt](licenses/android/NOTICE-files.txt)).
  `kotlin.time` derives from the ThreeTen backport, BSD-3-Clause
  ([licenses/android/ThreeTen-Backport.txt](licenses/android/ThreeTen-Backport.txt));
  a part of `kotlin.math` is under the Boost Software License 1.0, which asks for
  no notice in compiled form.
- Copyright: Copyright 2010-2024 JetBrains s.r.o and respective authors and
  developers; Copyright 2016-2021 JetBrains s.r.o and respective authors and
  developers (kotlinx.coroutines)
- Source: <https://github.com/JetBrains/kotlin/tree/v2.1.20/libraries/stdlib>,
  <https://github.com/Kotlin/kotlinx.coroutines/tree/1.6.4>,
  <https://github.com/JetBrains/java-annotations>

#### Guava ListenableFuture and JSpecify

Dependencies of AndroidX.

- Version: com.google.guava:listenablefuture 1.0, org.jspecify:jspecify 1.0.0
- License: Apache-2.0
- Copyright: Copyright The Guava Authors; Copyright The JSpecify Authors
- Source: <https://github.com/google/guava>, <https://github.com/jspecify/jspecify/tree/v1.0.0>

### Feather and Lucide icons in the Android app

The Android app draws the desktop's icons from vector drawables
(`res/drawable/ic_*.xml`), which `scripts/android-icons-from-svg.py` makes from
the desktop's `assets/icons/*.svg` and `assets/logo.svg`:

- `ic_settings`, `ic_export`, `ic_close` and `ic_connection` are derived from
  Feather's `settings`, `upload`, `x` and `server` icons (MIT).
- `ic_edit` is derived from Lucide's `pencil` icon (ISC).
- `ic_network`, `ic_log`, `ic_ping` (the speedometer), `ic_logo` and the plus
  signs are FreeTunnel's own.

- License: MIT (Feather) AND ISC (Lucide),
  [licenses/common/Feather-Lucide-icons.txt](licenses/common/Feather-Lucide-icons.txt)
- Copyright: Copyright (c) 2013-2023 Cole Bemis; Copyright (c) Lucide Icons and
  Contributors
- Source: <https://github.com/feathericons/feather>, <https://github.com/lucide-icons/lucide>

## Source code of the LGPL and GPL components

FreeTunnel ships these components as binaries built from source that it has
not changed:

- Qt 6.11.2 (LGPL-3.0, all desktop downloads): the source is at the addresses
  given under [Qt](#qt).
- The LGPL-licensed Ubuntu 20.04 libraries in the Linux packages:
  `bundled-system-packages.txt` names each source package and its version, and
  the source of each is at
  `https://launchpad.net/ubuntu/+source/<source package>/<version>`.
- The GCC runtime libraries in the Linux packages (GPL-3.0 with the GCC Runtime
  Library Exception): `gcc-runtime-source.tar` on the release the packages come
  from, as given under [GCC runtime libraries](#gcc-runtime-libraries).
- libfuse in the AppImage runtime (LGPL-2.1): `appimage-runtime.txt` in the
  AppImage names the runtime's commit, and the source is at the addresses
  given under [AppImage runtime](#appimage-runtime).

If any of this source is no longer available there, ask in FreeTunnel's issue
tracker, <https://github.com/dimmmmmmmer/freetunnel/issues>, and it will be
provided.

## License texts

Under [licenses/common/](licenses/common/): brotli, chromium-tldregistry,
cxxopts, Feather-Lucide-icons, fmt, http-parser, Inter-OFL-1.1, klib, ldns,
libevent, libsodium, libuv, llhttp, lwip, magic_enum, MPL-2.0, nghttp2, nghttp3,
ngtcp2, nlohmann-json, pcap_savefile, pcre2-10.37, tomlplusplus, zlib.

Under [licenses/desktop/](licenses/desktop/): AFL-2.1, AppImage-runtime,
Detours, GCC-exception-3.1, GPL-3.0, ICU-73, LGPL-2.1, LGPL-3.0,
Mesa-llvmpipe, Microsoft-redistributables, NSIS, QHotkey,
Qt-6.11.2-third-party, Wintun.

Under [licenses/android/](licenses/android/): ANTLR-4, BouncyCastle,
checker-qual, EPL-1.0, ktoml, NOTICE-files, qrcodegen, slf4j,
ThreeTen-Backport, winnow.
