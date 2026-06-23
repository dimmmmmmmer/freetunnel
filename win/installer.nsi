; FreeTunnel — NSIS Installer Script
; Installs to Program Files, requests admin elevation, creates uninstaller,
; Start Menu shortcut, and optional Desktop shortcut.

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
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

;--------------------------------
; Installer Section

Section "Install"
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
  ; Kill running instance
  nsExec::Exec 'taskkill /F /IM ${PRODUCT_EXE}'

  ; Remove files
  Delete "$INSTDIR\${PRODUCT_EXE}"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\Uninstall.exe"

  ; Remove every installed plugin/qml subdirectory and the install root.
  RMDir /r "$INSTDIR"

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

SectionEnd
