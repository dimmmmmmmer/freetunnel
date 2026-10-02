; FreeTunnel — NSIS Installer Script
; Installs to Program Files, requests admin elevation, creates uninstaller,
; Start Menu shortcut, and optional Desktop shortcut.

; NSIS 3 still defaults to an ANSI installer, which renders every non-Latin
; string in the current code page — so the Russian language table this script
; loads came out as mojibake for the users it was written for, and any install
; path with non-Latin characters was mangled too. Must appear before anything
; that emits strings.
Unicode true

!include "MUI2.nsh"
!include "FileFunc.nsh"

;--------------------------------
; General

!define PRODUCT_NAME      "FreeTunnel"
!define PRODUCT_PUBLISHER  "pnsrc"
!define PRODUCT_EXE        "FreeTunnel.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

; Version can be overridden from the command line: makensis /DPRODUCT_VERSION=1.0.0
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "1.0.0"
!endif

; The same version as four numbers, which is the only form VIProductVersion
; takes. It cannot be derived from PRODUCT_VERSION here: a build that is not from
; a tag carries "+dev.<run>", and makensis rejects that outright — so it arrives
; already numeric, computed alongside the display version.
!ifndef PRODUCT_VERSION_NUM
  !define PRODUCT_VERSION_NUM "1.0.0.0"
!endif

; Build dir containing compiled binaries — passed via /DBUILD_DIR=...
!ifndef BUILD_DIR
  !define BUILD_DIR "build\FreeTunnel"
!endif

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "FreeTunnel-${PRODUCT_VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; The installer had no version metadata of its own, so Setup.exe was a nameless
; unsigned binary asking for administrator rights — blank in Properties, and
; "Unknown" in the UAC prompt. This is also the file that was reported as
; Trojan:Win32/Bearfoos.B!ml (#34); an unidentified binary is one of the things
; those heuristics weigh.
VIProductVersion "${PRODUCT_VERSION_NUM}"
VIAddVersionKey "CompanyName"      "${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileDescription"  "${PRODUCT_NAME} installer"
VIAddVersionKey "FileVersion"      "${PRODUCT_VERSION}"
VIAddVersionKey "InternalName"     "${PRODUCT_NAME}"
VIAddVersionKey "LegalCopyright"   "Licensed under the Apache License 2.0"
VIAddVersionKey "OriginalFilename" "FreeTunnel-${PRODUCT_VERSION}-Setup.exe"
VIAddVersionKey "ProductName"      "${PRODUCT_NAME}"
VIAddVersionKey "ProductVersion"   "${PRODUCT_VERSION}"

;--------------------------------
; Interface

!define MUI_ICON   "..\assets\logo.ico"
!define MUI_UNICON "..\assets\logo.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "..\assets\installer-header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "..\assets\installer-welcome.bmp"
!define MUI_ABORTWARNING

;--------------------------------
; Pages

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
; The uninstaller recursively deletes $INSTDIR, so the only safe rule is to
; never take over a directory that already holds someone else's files. Checking
; at INSTALL time is what makes that deletion safe — checking at uninstall time
; cannot help, because by then our own exe is sitting in the user's D:\Tools and
; the directory looks like ours.
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Install directory safety

; Pushes "1" when $INSTDIR is new, empty or an earlier FreeTunnel install, and
; "0" when it already holds someone else's files. Uses $0 and $1.
Function CheckInstallDir
  ; New directory, or a previous FreeTunnel install being upgraded: fine.
  IfFileExists "$INSTDIR\*.*" 0 dirOk
  IfFileExists "$INSTDIR\${PRODUCT_EXE}" dirOk 0

  ; Existing directory with unrelated content.
  ClearErrors
  FindFirst $0 $1 "$INSTDIR\*.*"
  dirScan:
    StrCmp $1 "" dirScanDone
    StrCmp $1 "." dirNext
    StrCmp $1 ".." dirNext
    FindClose $0
    Push "0"
    Return
  dirNext:
    FindNext $0 $1
    Goto dirScan
  dirScanDone:
  FindClose $0

  dirOk:
  Push "1"
FunctionEnd

Function DirectoryLeave
  Call CheckInstallDir
  Pop $0
  StrCmp $0 "1" dirAccepted
    MessageBox MB_ICONEXCLAMATION|MB_OK \
      "$INSTDIR already contains other files.$\n$\nUninstalling FreeTunnel removes \
this folder and everything in it, so FreeTunnel will not install into a folder \
it does not own. Choose an empty or new folder."
    Abort
  dirAccepted:
FunctionEnd

; A silent install (/S) shows no pages, so the check above never ran for it and
; /D= could put FreeTunnel into any folder at all — which the uninstaller would
; later delete whole. Same check, with nobody to ask: the installer stops before
; touching anything, with exit code 2 for whatever started it.
Function .onInit
  IfSilent 0 initDone
    Call CheckInstallDir
    Pop $0
    StrCmp $0 "1" initDone
    SetErrorLevel 2
    Abort
  initDone:
FunctionEnd

;--------------------------------
; Languages

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

;--------------------------------
; Closing a running FreeTunnel

; Close every running FreeTunnel.exe and wait until none is left. Inserted into
; both the install and the uninstall section; uses $0 and $1.
;
; Politely first, and that matters: FreeTunnel holds a VPN tunnel and a
; privileged helper, and a forced kill leaves both up with nothing left to shut
; them down. taskkill without /F posts WM_CLOSE, which runs the app's own quit
; path — tunnel down, helper stopped, tray icon gone.
;
; Then wait for "no such process", which taskkill reports as exit code 128, and
; for nothing else. Any non-zero code used to count as closed, but taskkill also
; fails on a FreeTunnel.exe that has no window to post WM_CLOSE to: the elevated
; helper, which is the same exe and never has one, and the app itself once its
; window has closed while it is still shutting down. The loop then went straight
; on to files both still held. The helper quits by itself once the app has gone,
; after taking the tunnel down, so it is waited for like the app. A code this
; does not expect only makes the wait run its full length before the forced kill.
;
; The forced kill is waited for the same way, for a few seconds. taskkill /F
; only starts the exit: Windows still has to close everything the process held,
; its tunnel adapter among them, before it is gone and its files are free. A
; fixed second used to follow, after which the install could stop on "Error
; opening file for writing" and the uninstall leave files behind. Past the cap
; it goes on all the same, as it did; a stuck process must not block it.
!macro CLOSE_FREETUNNEL
  DetailPrint "Closing FreeTunnel if it is running..."
  StrCpy $1 0
  closeLoop:
    nsExec::Exec 'taskkill /IM "${PRODUCT_EXE}"'
    Pop $0
    StrCmp $0 "128" closed
    IntOp $1 $1 + 1
    ; ~10 s is generous for a clean shutdown; past that it is not coming down on
    ; its own and a stuck process must not block the install or uninstall forever.
    IntCmp $1 20 forceClose "" forceClose
    Sleep 500
    Goto closeLoop
  forceClose:
    DetailPrint "FreeTunnel did not exit; closing it forcibly."
    nsExec::Exec 'taskkill /F /IM "${PRODUCT_EXE}"'
    Pop $0
    StrCpy $1 0
  forceWait:
    nsExec::Exec 'taskkill /IM "${PRODUCT_EXE}"'
    Pop $0
    StrCmp $0 "128" closed
    IntOp $1 $1 + 1
    ; ~5 s, on top of the ~10 s above.
    IntCmp $1 10 stillExiting "" stillExiting
    Sleep 500
    Goto forceWait
  stillExiting:
    DetailPrint "FreeTunnel is still exiting; carrying on."
  closed:
!macroend

;--------------------------------
; Installer Section

Section "Install"
  ; Close a running FreeTunnel before touching a single file. Without this the
  ; install fails partway through on "file in use" — which is what everyone
  ; upgrading over a running copy hit, whether they used the in-app updater or
  ; downloaded the installer themselves.
  !insertmacro CLOSE_FREETUNNEL

  SetOutPath "$INSTDIR"

  ; Install the entire windeployqt output tree: the exe, every Qt DLL, and all
  ; plugin subdirectories. This MUST include qml/ — the QtQuick framework modules
  ; the UI imports at runtime (QtQuick, QtQuick.Controls, QtQuick.Layouts,
  ; QtQuick.Effects, Qt.labs.platform). Cherry-picking a fixed list of subdirs
  ; previously dropped qml/, so the QML engine couldn't load its imports, no
  ; window was created, and the app appeared not to launch. Recursing over the
  ; whole build dir also future-proofs against windeployqt adding new plugin dirs.
  File /r "${BUILD_DIR}\*"

  SetOutPath "$INSTDIR"

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Start Menu shortcut
  ;
  ; All Users, to match the install. Everything else here is per-machine —
  ; $PROGRAMFILES64, RequestExecutionLevel admin, every registry write to HKLM —
  ; but NSIS defaults $SMPROGRAMS and $DESKTOP to the *current* profile, which
  ; under elevation is whichever account UAC ran the installer as. The program
  ; then lands in Program Files for everyone while its shortcuts appear for one
  ; account, often not the one that asked for it.
  ; An install from before this was fixed left its copies in the running
  ; account's own folders. Take those away first, or upgrading leaves two of
  ; every shortcut.
  SetShellVarContext current
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

  SetShellVarContext all
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut  "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\assets\logo.ico"
  CreateShortCut  "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"       "$INSTDIR\Uninstall.exe" "" "$INSTDIR\assets\logo.ico"

  ; Desktop shortcut
  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\assets\logo.ico"

  ; Add/Remove Programs registry entry
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "DisplayName"     "${PRODUCT_NAME}"
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon"     "$INSTDIR\assets\logo.ico"
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "Publisher"       "${PRODUCT_PUBLISHER}"
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion"  "${PRODUCT_VERSION}"
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoRepair" 1

  ; Compute installed size
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "EstimatedSize" $0

  ; Windows Firewall: let FreeTunnel reach its VPN server, over TCP and UDP
  ; alike, where outgoing traffic is blocked unless a rule allows it.
  ;
  ; Outgoing only. An inbound rule used to sit beside this one, letting anyone on
  ; any network, public Wi-Fi included, open connections to FreeTunnel.exe — the
  ; elevated helper as much as the app. Nothing needs it: the app and the helper
  ; listen on 127.0.0.1 only (the helper's channel, and the VPN core's DNS proxy),
  ; the single-instance channel is a named pipe, and the TUN listener moves
  ; packets through the Wintun driver, not through sockets. Loopback is not
  ; filtered by the firewall, and replies to FreeTunnel's own connections to the
  ; server are let in without a rule.
  ;
  ; Deleting by name removes every rule of that name in both directions, so this
  ; is also what takes an earlier version's inbound rule away on upgrade.
  nsExec::Exec 'netsh advfirewall firewall delete rule name="${PRODUCT_NAME}"'
  nsExec::Exec 'netsh advfirewall firewall add rule name="${PRODUCT_NAME}" dir=out action=allow program="$INSTDIR\${PRODUCT_EXE}" enable=yes profile=any'

  ; URL protocol handlers: route freetunnel:// and tt:// links to the app
  ; (e.g. freetunnel://toggle, or a tt:// config-import link). --url-handler
  ; tells the app the URL was opened as a link, which any web page can do, and
  ; not run as a command: a link that would turn the VPN off is asked about.
  ; It goes before the URL, where nothing in the URL can take it off.
  WriteRegStr HKLM "Software\Classes\freetunnel" "" "URL:FreeTunnel Protocol"
  WriteRegStr HKLM "Software\Classes\freetunnel" "URL Protocol" ""
  WriteRegStr HKLM "Software\Classes\freetunnel\DefaultIcon" "" "$INSTDIR\${PRODUCT_EXE},0"
  WriteRegStr HKLM "Software\Classes\freetunnel\shell\open\command" "" '"$INSTDIR\${PRODUCT_EXE}" --url-handler "%1"'
  WriteRegStr HKLM "Software\Classes\tt" "" "URL:FreeTunnel Protocol"
  WriteRegStr HKLM "Software\Classes\tt" "URL Protocol" ""
  WriteRegStr HKLM "Software\Classes\tt\DefaultIcon" "" "$INSTDIR\${PRODUCT_EXE},0"
  WriteRegStr HKLM "Software\Classes\tt\shell\open\command" "" '"$INSTDIR\${PRODUCT_EXE}" --url-handler "%1"'

SectionEnd

;--------------------------------
; Uninstaller Section

Section "Uninstall"
  ; The way the installer does it, and for the same reason. This used to be a
  ; bare taskkill /F, which took the app and its elevated helper down mid-session
  ; with the tunnel still up and nothing left to close it. First, too: it uses
  ; $0, which holds the answer below.
  !insertmacro CLOSE_FREETUNNEL

  ; Is this actually our install directory? The directory page accepts any
  ; existing folder (e.g. D:\Tools), and an unconditional recursive delete there
  ; would take everything else in it with us. Decide BEFORE removing the very
  ; file we recognise ourselves by.
  StrCpy $0 "0"
  IfFileExists "$INSTDIR\${PRODUCT_EXE}" 0 +2
    StrCpy $0 "1"

  ; Remove files
  Delete "$INSTDIR\${PRODUCT_EXE}"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\Uninstall.exe"

  ; Remove every installed plugin/qml subdirectory and the install root.
  StrCmp $0 "1" 0 +3
    RMDir /r "$INSTDIR"
    Goto uninst_root_done
  RMDir "$INSTDIR"   ; unrecognised directory: only remove it if it is empty
  uninst_root_done:

  ; Remove shortcuts, from both places they may be. All Users is where this
  ; installer writes them; the current profile is where every build before it
  ; did, and an uninstall that leaves those behind leaves dead shortcuts.
  SetShellVarContext all
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
  SetShellVarContext current
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

  ; Remove Windows Firewall rules
  nsExec::Exec 'netsh advfirewall firewall delete rule name="${PRODUCT_NAME}"'

  ; Remove registry keys
  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "Software\Classes\freetunnel"
  DeleteRegKey HKLM "Software\Classes\tt"

  ; "Launch at system startup" is written by the app itself, under HKCU — the
  ; uninstaller only ever cleared HKLM, so the entry outlived the program and
  ; Windows went on trying to start a deleted executable at every logon. The value
  ; name is the one PlatformAutoStart.cpp writes.
  ;
  ; HKCU here is the hive of whoever is running the uninstaller elevated, which is
  ; not necessarily the user who turned the setting on. It covers the ordinary
  ; single-user machine; a leftover entry in another user's hive is that user's to
  ; clear, and is harmless beyond a failed launch.
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${PRODUCT_NAME}"

SectionEnd
