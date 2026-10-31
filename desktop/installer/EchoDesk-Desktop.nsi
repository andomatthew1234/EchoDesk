Unicode True
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32

!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "0.2.2"
!endif
!ifndef PUBLISH_DIR
  !error "PUBLISH_DIR must point to the packaged EchoDesk Desktop directory"
!endif
!ifndef OUTPUT_DIR
  !error "OUTPUT_DIR must point to the desktop dist directory"
!endif

!define PRODUCT_NAME "EchoDesk Desktop"
!define COMPANY_NAME "EchoDesk"
!define INSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\EchoDesk Desktop"

Name "${PRODUCT_NAME}"
Caption "${PRODUCT_NAME} ${PRODUCT_VERSION} Setup"
OutFile "${OUTPUT_DIR}\EchoDesk-Desktop-v${PRODUCT_VERSION}-win-x64-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\EchoDesk Desktop"
InstallDirRegKey HKCU "${INSTALL_KEY}" "InstallLocation"
BrandingText "EchoDesk"
ShowInstDetails show
ShowUninstDetails show

!include "MUI2.nsh"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\EchoDesk Desktop.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open EchoDesk Desktop"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "EchoDesk Desktop" SEC_MAIN
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "${PUBLISH_DIR}\*"
  WriteUninstaller "$INSTDIR\Uninstall EchoDesk Desktop.exe"

  CreateDirectory "$SMPROGRAMS\EchoDesk Desktop"
  CreateShortcut "$SMPROGRAMS\EchoDesk Desktop\EchoDesk Desktop.lnk" "$INSTDIR\EchoDesk Desktop.exe" "" "$INSTDIR\EchoDesk Desktop.exe" 0
  CreateShortcut "$SMPROGRAMS\EchoDesk Desktop\Uninstall EchoDesk Desktop.lnk" "$INSTDIR\Uninstall EchoDesk Desktop.exe"

  WriteRegStr HKCU "${INSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKCU "${INSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKCU "${INSTALL_KEY}" "Publisher" "${COMPANY_NAME}"
  WriteRegStr HKCU "${INSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${INSTALL_KEY}" "DisplayIcon" "$INSTDIR\EchoDesk Desktop.exe"
  WriteRegStr HKCU "${INSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall EchoDesk Desktop.exe"'
  WriteRegDWORD HKCU "${INSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${INSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  DetailPrint "Stopping EchoDesk background processes..."
  nsExec::ExecToLog 'taskkill /IM "EchoDesk Desktop.exe" /T /F'
  nsExec::ExecToLog 'taskkill /IM "EchoDesk Host.exe" /T /F'
  Delete "$SMPROGRAMS\EchoDesk Desktop\EchoDesk Desktop.lnk"
  Delete "$SMPROGRAMS\EchoDesk Desktop\Uninstall EchoDesk Desktop.lnk"
  RMDir "$SMPROGRAMS\EchoDesk Desktop"
  DeleteRegKey HKCU "${INSTALL_KEY}"
  RMDir /r "$INSTDIR"
SectionEnd
