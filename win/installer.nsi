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

Function DirectoryLeave
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
    MessageBox MB_ICONEXCLAMATION|MB_OK \
      "$INSTDIR already contains other files.$\n$\nUninstalling FreeTunnel removes \
this folder and everything in it, so FreeTunnel will not install into a folder \
it does not own. Choose an empty or new folder."
    Abort
  dirNext:
    FindNext $0 $1
    Goto dirScan
  dirScanDone:
  FindClose $0

  dirOk:
FunctionEnd

;--------------------------------
; Languages

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

;--------------------------------
; Installer Section

Section "Install"
  ; Close a running FreeTunnel before touching a single file. Without this the
  ; install fails partway through on "file in use" — which is what everyone
  ; upgrading over a running copy hit, whether they used the in-app updater or
  ; downloaded the installer themselves.
  ;
  ; Politely first, and that matters: FreeTunnel holds a VPN tunnel and a
  ; privileged helper, and a forced kill leaves both up with nothing left to shut
  ; them down. taskkill without /F posts WM_CLOSE, which runs the app's own quit
  ; path — tunnel down, helper stopped, tray icon gone.
  DetailPrint "Closing FreeTunnel if it is running..."
  StrCpy $1 0
  closeLoop:
    nsExec::Exec 'taskkill /IM "${PRODUCT_EXE}"'
    Pop $0
    ; Non-zero means "no such process": either it was never running or it has now
    ; finished shutting down.
    StrCmp $0 "0" 0 closed
    IntOp $1 $1 + 1
    ; ~10 s is generous for a clean shutdown; past that it is not coming down on
    ; its own and a stuck process must not block the upgrade forever.
    IntCmp $1 20 forceClose "" forceClose
    Sleep 500
    Goto closeLoop
  forceClose:
    DetailPrint "FreeTunnel did not exit; closing it forcibly."
    nsExec::Exec 'taskkill /F /IM "${PRODUCT_EXE}"'
    Pop $0
    Sleep 1000
  closed:

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

  ; Windows Firewall: allow the VPN client through (both TCP and UDP)
  nsExec::Exec 'netsh advfirewall firewall delete rule name="${PRODUCT_NAME}"'
  nsExec::Exec 'netsh advfirewall firewall add rule name="${PRODUCT_NAME}" dir=in action=allow program="$INSTDIR\${PRODUCT_EXE}" enable=yes profile=any'
  nsExec::Exec 'netsh advfirewall firewall add rule name="${PRODUCT_NAME}" dir=out action=allow program="$INSTDIR\${PRODUCT_EXE}" enable=yes profile=any'

  ; URL protocol handlers: route freetunnel:// and tt:// links to the app
  ; (e.g. freetunnel://toggle, or a tt:// config-import link).
  WriteRegStr HKLM "Software\Classes\freetunnel" "" "URL:FreeTunnel Protocol"
  WriteRegStr HKLM "Software\Classes\freetunnel" "URL Protocol" ""
  WriteRegStr HKLM "Software\Classes\freetunnel\DefaultIcon" "" "$INSTDIR\${PRODUCT_EXE},0"
  WriteRegStr HKLM "Software\Classes\freetunnel\shell\open\command" "" '"$INSTDIR\${PRODUCT_EXE}" "%1"'
  WriteRegStr HKLM "Software\Classes\tt" "" "URL:FreeTunnel Protocol"
  WriteRegStr HKLM "Software\Classes\tt" "URL Protocol" ""
  WriteRegStr HKLM "Software\Classes\tt\DefaultIcon" "" "$INSTDIR\${PRODUCT_EXE},0"
  WriteRegStr HKLM "Software\Classes\tt\shell\open\command" "" '"$INSTDIR\${PRODUCT_EXE}" "%1"'

SectionEnd

;--------------------------------
; Uninstaller Section

Section "Uninstall"
  ; Is this actually our install directory? The directory page accepts any
  ; existing folder (e.g. D:\Tools), and an unconditional recursive delete there
  ; would take everything else in it with us. Decide BEFORE removing the very
  ; file we recognise ourselves by.
  StrCpy $0 "0"
  IfFileExists "$INSTDIR\${PRODUCT_EXE}" 0 +2
    StrCpy $0 "1"

  ; Kill running instance
  nsExec::Exec 'taskkill /F /IM ${PRODUCT_EXE}'

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

  ; Remove shortcuts
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
