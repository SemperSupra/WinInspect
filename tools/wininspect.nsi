!include "MUI2.nsh"

!ifndef VERSION
  !define VERSION "dev"
!endif

!ifndef BUILD_SRC
  !define BUILD_SRC "..\build\Release"
!endif

Name "WinInspect ${VERSION}"
OutFile "..\\dist\\WinInspect-Installer.exe"
InstallDir "$PROGRAMFILES64\\WinInspect"
RequestExecutionLevel admin

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Section "WinInspect Core" SecCore
  SetOutPath "$INSTDIR"
  
  ; Binaries (assuming they are in build/Release/)
  File "${BUILD_SRC}\wininspectd.exe"
  File "${BUILD_SRC}\wininspect.exe"
  File "${BUILD_SRC}\wininspect-gui.exe"
  
  ; License
  File "..\LICENSE"
  
  ; Registry keys for uninstaller
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayName" "WinInspect"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteUninstaller "$INSTDIR\uninstall.exe"
  
  ; Shortcuts
  CreateDirectory "$SMPROGRAMS\WinInspect"
  CreateShortcut "$SMPROGRAMS\WinInspect\WinInspect GUI.lnk" "$INSTDIR\wininspect-gui.exe"
  CreateShortcut "$SMPROGRAMS\WinInspect\Uninstall WinInspect.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\wininspectd.exe"
  Delete "$INSTDIR\wininspect.exe"
  Delete "$INSTDIR\wininspect-gui.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\uninstall.exe"
  
  RMDir "$INSTDIR"
  
  Delete "$SMPROGRAMS\WinInspect\WinInspect GUI.lnk"
  Delete "$SMPROGRAMS\WinInspect\Uninstall WinInspect.lnk"
  RMDir "$SMPROGRAMS\WinInspect"
  
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect"
SectionEnd
