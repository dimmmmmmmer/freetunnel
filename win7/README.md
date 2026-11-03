# FreeTunnel on Windows 7 (experimental)

FreeTunnel's releases need Windows 10 or 11. This folder and
[`.github/workflows/win7.yml`](../.github/workflows/win7.yml) produce a separate,
**unofficial** build for Windows 7 SP1 (x64). It is not published with
releases, is not a required check, and nobody has yet run it on a real
Windows 7 machine — see [What has been checked](#what-has-been-checked).

## Getting the build

Actions → **Windows 7 (experimental)** → **Run workflow**. The run also starts
on any pull request that changes the files it is built from. Its artifact,
`freetunnel-windows7-x86_64`, holds:

| File | What it is |
| --- | --- |
| `freetunnel-windows7-x86_64-Setup.exe` | The usual NSIS installer, with the VPN helper service |
| `freetunnel-windows7-x86_64-portable.zip` | The same files, unpacked. Connecting then asks for administrator rights through UAC each time |
| `win7-import-check.md` | What [`scripts/check-win7-imports.py`](../scripts/check-win7-imports.py) found |

The first run builds OpenSSL and Qt, which takes a few hours. Later runs reuse
them from the Actions cache and take about as long as the regular Windows build.

## What Windows 7 needs

- **Windows 7 SP1, 64-bit**, with these updates (any Windows 7 that was kept up
  to date through Windows Update has them):

  | Update | Why |
  | --- | --- |
  | KB2533623 | `SetDefaultDllDirectories`, which Qt calls at startup |
  | KB2670838 (Platform Update) | Direct3D 11.1 and DXGI 1.2, which Qt Quick draws with |
  | KB4474419 and KB4490628 | SHA-2 code signing: without them Windows refuses to load the Wintun driver, and the VPN cannot create its adapter |

- **Up-to-date root certificates** for HTTPS inside the app (update checks,
  subscriptions, geo lists): Windows 7 stopped receiving them automatically, so
  a server certificate from a newer root (Let's Encrypt's ISRG Root X1, for
  example) may be rejected.

The C and C++ runtimes are in the package, so KB2999226 (the Universal C
Runtime) is not needed.

## How it differs from the Windows release

| | Windows release | Windows 7 build |
| --- | --- | --- |
| Qt | Official Qt 6.8.3 binaries (Windows 10+) | Qt 6.8.5 built from source with the [crystalidea/qt6windows7](https://github.com/crystalidea/qt6windows7) qtbase backport |
| HTTPS in the app (Qt) | Schannel | OpenSSL 3.5.9 linked into Qt's TLS plugin |
| BoringSSL in the VPN core | NLC's recipe | The same, plus [`boringssl-win7-rand-fallback.patch`](boringssl-win7-rand-fallback.patch) |
| Compiler | `windows-latest`'s Visual Studio | Visual Studio 2022 (`windows-2022`) |
| C runtime | VC++ runtime DLLs | VC++ runtime DLLs and the app-local Universal CRT |

Why each change is needed:

- **Qt.** Qt 6 calls functions that first appeared in Windows 8 and 10
  (`WaitOnAddress`, `SetThreadDescription`, `CreateDXGIFactory2`,
  per-monitor DPI, WinRT), so the official DLLs do not even load on Windows 7.
  The backport resolves each of them at run time and falls back to the Windows 7
  way when it is missing; Windows 8 and later keep Qt's own path. It is written
  for Qt 6.8.5, whose LGPL source is public; FreeTunnel uses nothing that 6.8.3
  did not have.
- **Qt's TLS.** Qt 6.8's Schannel backend hands Windows `SCH_CREDENTIALS`, which
  only Windows 10 1809 and later accept, so every HTTPS request would fail on
  Windows 7. The backport's own builds use OpenSSL instead, and so does this
  one. OpenSSL is built with `no-dso no-module no-engine` and keeps its default
  `C:\Program Files\...` directories, so no file a non-administrator can create
  is ever read as its configuration or loaded as code.
- **BoringSSL.** Current BoringSSL gets randomness only from `ProcessPrng`,
  which Windows 7 does not have, and calls `abort()` when it is missing — the
  VPN would die on its first connection. The patch falls back to `RtlGenRandom`
  (`advapi32!SystemFunction036`), which BoringSSL used before, only when
  `ProcessPrng` cannot be found.
- **Compiler.** The runtime of the newest Visual Studio is moving away from
  Windows 7; Visual Studio 2022's still supports it.

## What has been checked

In CI, on every run:

- the BoringSSL patch is in the recipe revision conan builds, and its fallback
  is in `FreeTunnel.exe`;
- Qt's TLS plugin has OpenSSL linked in and the Schannel plugin is gone;
- every `.exe` and `.dll` in the package is read by
  `scripts/check-win7-imports.py`, which fails the run on an import that
  Windows 7 is known not to have (a Windows 8+ DLL or API set, a Windows 8+
  function, an .exe whose PE header asks for Windows 8+). It knows the usual suspects,
  not every Windows 7 export table, so a clean report means "nothing known to
  be missing", not "works".

Not checked: starting it on Windows 7. The VPN core itself targets Windows 7
(`_WIN32_WINNT=0x0601`) and Wintun 0.14.1 supports it, but whether the window
draws, the tray icon and hotkeys work, the helper service installs and a
connection comes up has to be tried on a real machine. If you do, please open
an issue with what happened.

## If it does not start

- "The procedure entry point ... could not be located in ...": an import the
  check did not know about. Please report the function and DLL names.
- A black or empty window: try `set QSG_RHI_BACKEND=opengl` before starting
  `FreeTunnel.exe` from a command prompt, which makes Qt Quick draw with OpenGL
  instead of Direct3D 11.
- No HTTPS (updates or subscriptions fail with a certificate error): update the
  root certificates.

## Files here

| File | What it is |
| --- | --- |
| [`boringssl-win7-rand-fallback.patch`](boringssl-win7-rand-fallback.patch) | The BoringSSL patch, added to NLC's recipe by `scripts/export-patched-boringssl.sh` when `FT_BORINGSSL_EXTRA_PATCHES` names it |
| [`NOTICE-Windows7.txt`](NOTICE-Windows7.txt) | Shipped in the package: what this build carries beyond `THIRD_PARTY_NOTICES.md`, and where the source of the modified Qt is |
