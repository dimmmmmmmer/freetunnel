; TrustTunnel Qt Client — NSIS Installer Script
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

  ; Main executable and assets
  File "${BUILD_DIR}\${PRODUCT_EXE}"
  File /nonfatal "${BUILD_DIR}\*.dll"

  ; Qt plugins required at runtime
  SetOutPath "$INSTDIR\platforms"
  File /nonfatal "${BUILD_DIR}\platforms\*.dll"

  SetOutPath "$INSTDIR\styles"
  File /nonfatal "${BUILD_DIR}\styles\*.dll"

  SetOutPath "$INSTDIR\tls"
  File /nonfatal "${BUILD_DIR}\tls\*.dll"

  SetOutPath "$INSTDIR\imageformats"
  File /nonfatal "${BUILD_DIR}\imageformats\*.dll"

  ; Assets
  SetOutPath "$INSTDIR\assets"
  File /nonfatal "${BUILD_DIR}\assets\logo.png"
  File /nonfatal "${BUILD_DIR}\assets\LICENSE"

  SetOutPath "$INSTDIR"

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Start Menu shortcut
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut  "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}"
  CreateShortCut  "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"       "$INSTDIR\Uninstall.exe"

  ; Desktop shortcut
  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}"

  ; Add/Remove Programs registry entry
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "DisplayName"     "${PRODUCT_NAME}"
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${PRODUCT_EXE}"
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

  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\tls"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\assets"
  RMDir    "$INSTDIR"

  ; Remove shortcuts
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

  ; Remove Windows Firewall rules
  nsExec::Exec 'netsh advfirewall firewall delete rule name="${PRODUCT_NAME}"'

  ; Remove registry keys
  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"

SectionEnd
