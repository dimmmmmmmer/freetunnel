#!/usr/bin/env python3
"""Check a deployed Windows build for imports Windows 7 cannot satisfy.

Used by .github/workflows/win7.yml on the folder windeployqt filled. Nothing
here runs a binary: it reads the import tables of every .exe and .dll under the
folder (static and delay-load) and looks each entry up in what is known about
Windows 7 SP1. The Windows loader refuses to start a process when one of its
static imports, or one of its DLLs' static imports, is missing, so a single
Windows 8+ entry point anywhere in the folder is enough to make the whole
application fail on Windows 7 with "The procedure entry point ... could not be
located".

The lists below are what is known, not the complete Windows 7 export tables,
which are not available on a CI runner. So:

  FAIL  a DLL that is not on Windows 7 (combase, shcore, an API set newer
        than Windows 7's ...), a function that was added in Windows 8 or
        later, or a PE header that asks for a Windows newer than 6.1.
  WARN  a DLL neither bundled nor known to be on Windows 7, a delay-loaded
        Windows 8+ function (fine when the code checks the Windows version
        before calling it, a crash when it does not), or a function that needs
        a Windows 7 update.

A clean report therefore means "nothing known to be missing", and the build
still has to be started on a real Windows 7 to be called working.

Exit status: 0 when nothing FAILs, 1 otherwise, 2 on a usage error.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass, field

# Windows 7 SP1 version, the newest a PE header may ask for.
WIN7_VERSION = (6, 1)

# System DLLs a Windows 7 SP1 installation has (lowercase). Some need an update
# that any maintained Windows 7 has; those are listed in the README of win7/.
WIN7_DLLS = frozenset(
    """
    activeds advapi32 apphelp authz avrt bcrypt bcryptprimitives cabinet
    cfgmgr32 clbcatq comctl32 comdlg32 credui crypt32 cryptbase cryptnet
    cryptsp cryptui d2d1 d3d10 d3d10_1 d3d10core d3d11 d3d9 dbgeng dbghelp
    devobj dhcpcsvc dhcpcsvc6 dnsapi dsound dwmapi dwrite dxgi dxva2 evr
    faultrep fwpuclnt gdi32 gdiplus glu32 hid iertutil imagehlp imm32
    iphlpapi kernel32 kernelbase ksuser logoncli mf mfplat mfreadwrite
    mpr msasn1 msi msimg32 msvcrt mswsock ncrypt netapi32 netutils newdev
    normaliz nsi ntdll ntdsapi ntmarta ole32 oleacc oleaut32 opengl32 pdh
    powrprof profapi propsys psapi rasapi32 rpcrt4 samcli sechost secur32
    setupapi shell32 shlwapi srvcli sspicli sxs tdh uiautomationcore
    urlmon user32 userenv usp10 uxtheme version wer wevtapi windowscodecs winhttp
    wininet winmm winspool.drv winsta wintrust winusb wkscli wlanapi wldap32
    ws2_32 wsock32 wtsapi32 xinput9_1_0
    """.split()
)

# DLLs that first shipped with Windows 8 or later.
WIN8_PLUS_DLLS = frozenset(
    """
    bcp47langs combase coremessaging d3d12 dcomp dxcore icu mrmcorer shcore
    twinapi.appcore windows.storage wintypes xinput1_4
    """.split()
)

# The API sets in Windows 7's schema. Anything else named api-ms-win-* (bar the
# CRT, which has to be bundled) or ext-ms-* came with Windows 8 or later.
WIN7_APISETS = frozenset(
    "api-ms-win-" + name
    for name in """
    core-console-l1-1-0 core-datetime-l1-1-0 core-debug-l1-1-0
    core-delayload-l1-1-0 core-errorhandling-l1-1-0 core-fibers-l1-1-0
    core-file-l1-1-0 core-handle-l1-1-0 core-heap-l1-1-0
    core-interlocked-l1-1-0 core-io-l1-1-0 core-libraryloader-l1-1-0
    core-localization-l1-1-0 core-localregistry-l1-1-0 core-memory-l1-1-0
    core-misc-l1-1-0 core-namedpipe-l1-1-0 core-processenvironment-l1-1-0
    core-processthreads-l1-1-0 core-profile-l1-1-0 core-rtlsupport-l1-1-0
    core-string-l1-1-0 core-synch-l1-1-0 core-sysinfo-l1-1-0
    core-threadpool-l1-1-0 core-ums-l1-1-0 core-util-l1-1-0
    core-xstate-l1-1-0 security-base-l1-1-0 security-lsalookup-l1-1-0
    security-sddl-l1-1-0 service-core-l1-1-0 service-management-l1-1-0
    service-management-l2-1-0 service-winsvc-l1-1-0
    """.split()
)

# Functions added in Windows 8 or later, from the system DLLs above. Not every
# such function: the ones a C++/Qt/networking program can plausibly import.
WIN8_PLUS_FUNCS = frozenset(
    """
    AdjustWindowRectExForDpi AppPolicyGetProcessTerminationMethod
    AppPolicyGetThreadInitializationType AppPolicyGetWindowingModel
    AreDpiAwarenessContextsEqual BCryptCreateMultiHash BCryptHash
    BCryptKeyDerivation BCryptProcessMultiOperations CM_Register_Notification
    CM_Unregister_Notification CoDecrementMTAUsage CoIncrementMTAUsage
    CoWaitForMultipleObjects CopyFile2 CreateAppContainerProfile
    CreateDXGIFactory2 CreateDirect3D11DeviceFromDXGIDevice
    CreateDirect3D11SurfaceFromDXGISurface CreateFile2 CreateFileMapping2
    CreateFileMappingFromApp D3D11On12CreateDevice DXGIDeclareAdapterRemovalSupport
    DXGIGetDebugInterface1 DeleteAppContainerProfile DeleteSynchronizationBarrier
    DeriveAppContainerSidFromAppContainerName DiscardVirtualMemory
    DnsCancelQuery DnsGetApplicationSettings DnsQueryEx DnsServiceBrowse
    DnsServiceRegister DnsServiceResolve DnsSetApplicationSettings
    EnableMouseInPointer EnableNonClientDpiScaling
    EnterSynchronizationBarrier EnumDynamicTimeZoneInformation
    EvaluateProximityToPolygon EvaluateProximityToRect EventSetInformation
    FreeInterfaceDnsSettings GetAddrInfoExCancel GetAddrInfoExOverlappedResult
    GetAppContainerFolderPath GetApplicationUserModelId GetAutoRotationState
    GetAwarenessFromDpiAwarenessContext GetCIMSSM
    GetCurrentApplicationUserModelId GetCurrentInputMessageSource
    GetCurrentPackageFamilyName GetCurrentPackageFullName GetCurrentPackageId
    GetCurrentPackagePath GetCurrentThreadStackLimits
    GetDialogControlDpiChangeBehavior GetDialogDpiChangeBehavior
    GetDisplayAutoRotationPreferences GetDpiForMonitor GetDpiForSystem
    GetDpiForWindow GetDpiFromDpiAwarenessContext
    GetDynamicTimeZoneInformationEffectiveYears GetFirmwareType
    GetInterfaceDnsSettings GetIpNetworkConnectionBandwidthEstimates
    GetMemoryErrorHandlingCapabilities GetNetworkConnectivityHint
    GetOverlappedResultEx GetPackageFamilyName GetPackageFullName GetPackageId
    GetPackagePath GetPackagePathByFullName GetPackagesByPackageFamily
    GetPointerCursorId GetPointerDevice GetPointerDeviceProperties
    GetPointerDeviceRects GetPointerDevices GetPointerFrameInfo
    GetPointerFrameInfoHistory GetPointerFramePenInfo GetPointerFrameTouchInfo
    GetPointerInfo GetPointerInfoHistory GetPointerPenInfo GetPointerTouchInfo
    GetPointerType GetProcessDpiAwareness GetProcessInformation
    GetProcessMitigationPolicy GetRawPointerDeviceData GetScaleFactorForMonitor
    GetSystemCpuSetInformation GetSystemDpiForProcess GetSystemMetricsForDpi
    GetSystemTimeAdjustmentPrecise GetSystemTimePreciseAsFileTime
    GetThreadDescription GetThreadDpiAwarenessContext GetThreadDpiHostingBehavior
    GetThreadInformation GetUserDefaultGeoName GetWindowDpiAwarenessContext
    GetWindowDpiHostingBehavior GetWindowFeedbackSetting InitializeSynchronizationBarrier
    InitializeTouchInjection InjectTouchInput IsMouseInPointerEnabled
    IsValidDpiAwarenessContext IsWow64Process2 LoadPackagedLibrary
    LogicalToPhysicalPointForPerMonitorDPI MapViewOfFile3 MapViewOfFileFromApp
    MapViewOfFileNuma2 NotifyNetworkConnectivityHintChange OfferVirtualMemory
    OpenFileMappingFromApp OpenThemeDataForDpi PackTouchHitTestingProximityEvaluation
    PackageIdFromFullName PhysicalToLogicalPointForPerMonitorDPI
    PowerRegisterSuspendResumeNotification PowerUnregisterSuspendResumeNotification
    PrefetchVirtualMemory ProcessPrng PssCaptureSnapshot QueryInterruptTime
    QueryInterruptTimePrecise QueryUnbiasedInterruptTimePrecise
    ReclaimVirtualMemory RegisterBadMemoryNotification
    RegisterPointerDeviceNotifications RegisterSuspendResumeNotification
    RegisterTouchHitTestingWindow RoActivateInstance RoGetActivationFactory
    RoGetAgileReference RoInitialize RoUninitialize SetCoalescableTimer
    SetDialogControlDpiChangeBehavior SetDialogDpiChangeBehavior
    SetDisplayAutoRotationPreferences SetInterfaceDnsSettings
    SetProcessDpiAwareness SetProcessDpiAwarenessContext SetProcessInformation
    SetProcessMitigationPolicy SetThreadDescription SetThreadDpiAwarenessContext
    SetThreadDpiHostingBehavior SetThreadInformation SetThreadSelectedCpuSets
    SetThreadpoolTimerEx SetThreadpoolWaitEx SetUserGeoName
    SetWindowFeedbackSetting SkipPointerFrameMessages SystemParametersInfoForDpi
    UnregisterSuspendResumeNotification VirtualAlloc2 VirtualAllocFromApp
    VirtualProtectFromApp WaitOnAddress WakeByAddressAll WakeByAddressSingle
    WinHttpCreateProxyResolver WinHttpFreeProxyResult WinHttpGetProxyForUrlEx
    WinHttpGetProxyResult WinHttpWebSocketClose WinHttpWebSocketCompleteUpgrade
    WinHttpWebSocketQueryCloseStatus WinHttpWebSocketReceive WinHttpWebSocketSend
    WinHttpWebSocketShutdown WindowsCreateString WindowsCreateStringReference
    WindowsDeleteString WindowsGetStringRawBuffer
    """.split()
)

# On Windows 7 only with an update installed: the function and the update.
WIN7_UPDATE_FUNCS = {
    "AddDllDirectory": "KB2533623",
    "RemoveDllDirectory": "KB2533623",
    "SetDefaultDllDirectories": "KB2533623",
}

IMAGE_DIRECTORY_ENTRY_IMPORT = 1
IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT = 13


class PeError(Exception):
    """The file is not a PE image this script can read."""


@dataclass
class Imports:
    """What one PE file asks the loader for."""

    subsystem_version: tuple[int, int] = (0, 0)
    os_version: tuple[int, int] = (0, 0)
    # dll name (lowercase) -> function names (ordinals as "#n")
    static: dict[str, list[str]] = field(default_factory=dict)
    delay: dict[str, list[str]] = field(default_factory=dict)


class PeImage:
    """Just enough of a PE reader for the import tables."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        if data[:2] != b"MZ":
            raise PeError("no MZ header")
        pe = self._u32(0x3C)
        if data[pe:pe + 4] != b"PE\0\0":
            raise PeError("no PE signature")
        coff = pe + 4
        self.sections_count = self._u16(coff + 2)
        opt_size = self._u16(coff + 16)
        opt = coff + 20
        magic = self._u16(opt)
        if magic == 0x20B:
            self.pe32plus = True
            dirs = opt + 112
        elif magic == 0x10B:
            self.pe32plus = False
            dirs = opt + 96
        else:
            raise PeError(f"unknown optional header magic {magic:#x}")
        self.os_version = (self._u16(opt + 40), self._u16(opt + 42))
        self.subsystem_version = (self._u16(opt + 48), self._u16(opt + 50))
        self.dir_count = self._u32(dirs - 4)
        self.dirs = dirs
        self.sections = []
        sec = opt + opt_size
        for i in range(self.sections_count):
            base = sec + 40 * i
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, base + 8)
            self.sections.append((vaddr, max(vsize, rawsize), rawptr, rawsize))

    def _u16(self, off: int) -> int:
        return struct.unpack_from("<H", self.data, off)[0]

    def _u32(self, off: int) -> int:
        return struct.unpack_from("<I", self.data, off)[0]

    def _u64(self, off: int) -> int:
        return struct.unpack_from("<Q", self.data, off)[0]

    def directory(self, index: int) -> tuple[int, int]:
        if index >= self.dir_count:
            return 0, 0
        return struct.unpack_from("<II", self.data, self.dirs + 8 * index)

    def offset(self, rva: int) -> int:
        for vaddr, size, rawptr, rawsize in self.sections:
            if vaddr <= rva < vaddr + size:
                delta = rva - vaddr
                if delta >= rawsize:
                    raise PeError(f"RVA {rva:#x} points past the raw data of its section")
                return rawptr + delta
        raise PeError(f"RVA {rva:#x} is in no section")

    def cstring(self, rva: int) -> str:
        off = self.offset(rva)
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("ascii", errors="replace")

    def _thunks(self, rva: int) -> list[str]:
        names = []
        if not rva:
            return names
        off = self.offset(rva)
        step = 8 if self.pe32plus else 4
        flag = 1 << (63 if self.pe32plus else 31)
        while True:
            value = self._u64(off) if self.pe32plus else self._u32(off)
            if value == 0:
                break
            if value & flag:
                names.append(f"#{value & 0xFFFF}")
            else:
                names.append(self.cstring((value & 0x7FFFFFFF) + 2))
            off += step
        return names

    def imports(self) -> Imports:
        result = Imports(self.subsystem_version, self.os_version)
        rva, size = self.directory(IMAGE_DIRECTORY_ENTRY_IMPORT)
        if rva and size:
            off = self.offset(rva)
            while True:
                oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", self.data, off)
                if not (oft or name_rva or ft):
                    break
                dll = self.cstring(name_rva).lower()
                result.static.setdefault(dll, []).extend(self._thunks(oft or ft))
                off += 20
        rva, size = self.directory(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)
        if rva and size:
            off = self.offset(rva)
            while True:
                attrs, name_rva, _, iat, int_rva = struct.unpack_from("<IIIII", self.data, off)
                if not name_rva:
                    break
                if attrs & 1:
                    dll = self.cstring(name_rva).lower()
                    result.delay.setdefault(dll, []).extend(self._thunks(int_rva or iat))
                off += 32
        return result


def stem(dll: str) -> str:
    """kernel32.dll -> kernel32 (winspool.drv keeps its extension)."""
    return dll[:-4] if dll.endswith(".dll") else dll


def classify_dll(dll: str, bundled: set[str]) -> tuple[str, str] | None:
    """(level, reason) for a DLL that is a problem, None when it is fine."""
    if dll in bundled:
        return None
    name = stem(dll)
    if name.startswith("api-ms-win-crt-") or name in ("ucrtbase", "vcruntime140", "vcruntime140_1", "msvcp140"):
        return "FAIL", "the C/C++ runtime is not bundled (Windows 7 has no UCRT without KB2999226)"
    if name.startswith("ext-ms-"):
        return "FAIL", "extension API sets came with Windows 8"
    if name.startswith("api-ms-win-"):
        if name in WIN7_APISETS:
            return None
        return "FAIL", "API set not in Windows 7's schema"
    if name in WIN8_PLUS_DLLS:
        return "FAIL", "this DLL first shipped with Windows 8 or later"
    if name in WIN7_DLLS:
        return None
    return "WARN", "neither bundled nor known to be part of Windows 7"


@dataclass
class Finding:
    level: str
    file: str
    text: str


def check_file(path: str, rel: str, bundled: set[str]) -> list[Finding]:
    with open(path, "rb") as fh:
        data = fh.read()
    try:
        image = PeImage(data)
        imp = image.imports()
    except (PeError, struct.error, ValueError) as exc:
        return [Finding("WARN", rel, f"could not read the import table: {exc}")]

    findings = []
    if imp.subsystem_version > WIN7_VERSION:
        findings.append(Finding(
            "FAIL", rel,
            "PE subsystem version %d.%d is newer than Windows 7 (6.1); the loader refuses it" % imp.subsystem_version))
    if imp.os_version > WIN7_VERSION:
        findings.append(Finding("WARN", rel, "PE OS version %d.%d is newer than Windows 7 (6.1)" % imp.os_version))

    for kind, table in (("static", imp.static), ("delay", imp.delay)):
        for dll, funcs in sorted(table.items()):
            verdict = classify_dll(dll, bundled)
            if verdict:
                level, reason = verdict
                if kind == "delay":
                    level = "WARN"
                findings.append(Finding(level, rel, f"{kind} import of {dll}: {reason}"))
                continue
            if dll in bundled:
                continue
            for func in funcs:
                if func in WIN8_PLUS_FUNCS:
                    if kind == "static":
                        findings.append(Finding("FAIL", rel, f"imports {dll}!{func}, added in Windows 8 or later"))
                    else:
                        findings.append(Finding(
                            "WARN", rel,
                            f"delay-loads {dll}!{func} (Windows 8+): a crash on Windows 7 if called unguarded"))
                elif func in WIN7_UPDATE_FUNCS:
                    findings.append(Finding(
                        "WARN", rel, f"imports {dll}!{func}: on Windows 7 only with {WIN7_UPDATE_FUNCS[func]}"))
    return findings


def scan(root: str) -> tuple[list[str], set[str]]:
    binaries = []
    bundled = set()
    for dirpath, _, files in os.walk(root):
        for name in files:
            lower = name.lower()
            if lower.endswith((".exe", ".dll")):
                binaries.append(os.path.join(dirpath, name))
                if lower.endswith(".dll"):
                    bundled.add(lower)
    return sorted(binaries), bundled


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("root", help="the deployed folder (the one with FreeTunnel.exe)")
    parser.add_argument("--report", help="also write the findings to this Markdown file")
    parser.add_argument(
        "--expect-string", action="append", default=[], metavar="FILE=TEXT",
        help="fail unless FILE (relative to root) contains TEXT as ASCII bytes")
    args = parser.parse_args()

    if not os.path.isdir(args.root):
        print(f"check-win7-imports: {args.root} is not a directory", file=sys.stderr)
        return 2

    binaries, bundled = scan(args.root)
    if not binaries:
        print(f"check-win7-imports: no .exe or .dll under {args.root}", file=sys.stderr)
        return 2

    findings: list[Finding] = []
    for path in binaries:
        findings.extend(check_file(path, os.path.relpath(path, args.root), bundled))

    for spec in args.expect_string:
        rel, sep, text = spec.partition("=")
        if not sep or not text:
            print(f"check-win7-imports: --expect-string wants FILE=TEXT, got {spec!r}", file=sys.stderr)
            return 2
        target = os.path.join(args.root, rel)
        try:
            with open(target, "rb") as fh:
                present = text.encode("ascii") in fh.read()
        except OSError as exc:
            findings.append(Finding("FAIL", rel, f"cannot read it to look for {text!r}: {exc}"))
            continue
        if not present:
            findings.append(Finding("FAIL", rel, f"does not contain {text!r}"))

    fails = [f for f in findings if f.level == "FAIL"]
    warns = [f for f in findings if f.level == "WARN"]
    lines = [
        "# Windows 7 import check",
        "",
        f"{len(binaries)} binaries scanned, {len(bundled)} DLLs bundled: "
        f"**{len(fails)} failure(s)**, {len(warns)} warning(s).",
        "",
    ]
    if findings:
        lines += ["| Level | File | Finding |", "| --- | --- | --- |"]
        lines += [f"| {f.level} | `{f.file}` | {f.text} |" for f in fails + warns]
    else:
        lines.append("Nothing known to be missing on Windows 7.")
    report = "\n".join(lines) + "\n"

    for f in fails + warns:
        print(f"{f.level}: {f.file}: {f.text}")
    print(f"check-win7-imports: {len(binaries)} binaries, {len(fails)} failure(s), {len(warns)} warning(s)")
    if args.report:
        with open(args.report, "w", encoding="utf-8") as fh:
            fh.write(report)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
