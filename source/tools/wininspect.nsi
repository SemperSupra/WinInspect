!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"

!ifndef VERSION
  !define VERSION "dev"
!endif

!ifndef BUILD_SRC
  !define BUILD_SRC "..\build\Release"
!endif

!ifndef DIST_DIR
  !define DIST_DIR "..\dist"
!endif

!ifndef VCREDIST_URL
  !define VCREDIST_URL "https://aka.ms/vs/17/release/vc_redist.x64.exe"
!endif

Name "WinInspect ${VERSION}"
OutFile "${DIST_DIR}\\WinInspect-Installer-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\\WinInspect"
RequestExecutionLevel user

; ── Branding ─────────────────────────────────────────────────────────────────
!define MUI_ICON "..\assets\brand\ico\strix.ico"
!define MUI_UNICON "..\assets\brand\ico\strix.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "WinInspect ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "Window inspection for Windows and Wine.$\r$\n$\r$\n\
  Strix the Window Owl inspects Windows and Wine desktops$\r$\n\
  on your behalf.$\r$\n$\r$\n\
  Click Install to continue."
!define MUI_FINISHPAGE_TITLE "Installation Complete"
!define MUI_FINISHPAGE_TEXT "WinInspect ${VERSION} has been installed.$\r$\n$\r$\n\
  $\"Strix the Window Owl$\" is your agent.$\r$\n\
  The magnifying glass is WinInspect."
!define MUI_FINISHPAGE_RUN "$INSTDIR\wininspect-gui.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch WinInspect GUI"

; ── Pages ────────────────────────────────────────────────────────────────────
!insertmacro MUI_PAGE_WELCOME

; License pages — one per type, skipped by pre-callback based on $license_type.
; Uses native Page command (not MUI) for precise pre-callback control.
LicenseForceSelection checkbox "I accept the license terms"
LicenseData "..\assets\license\polyform-nc-1.0.0.txt"
Page license preLicenseNC "" ""
LicenseData "..\assets\license\commercial.txt"
Page license preLicenseCommercial "" ""

!insertmacro MUI_PAGE_DIRECTORY
!define MUI_PAGE_HEADER_TEXT "Choose Components"
!define MUI_PAGE_HEADER_SUBTEXT "Select which components to install."
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_UNWELCOMEPAGE_TITLE "Uninstall WinInspect"
!define MUI_UNWELCOMEPAGE_TEXT "This will remove WinInspect ${VERSION} from your system.$\r$\n$\r$\n\
  Window inspection for Windows and Wine."
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

ShowInstDetails show

; ── Variables ────────────────────────────────────────────────────────────────
Var install_mode
Var fleet_mode
Var is_wine
Var license_type    ; "noncommercial" (default) or "commercial" (/LICENSE-TYPE)

; Existing install detection (set by DetectExistingInstall)
Var existing_version   ; version string from existing install, or ""
Var existing_method    ; "none", "nsis", "chocolatey", "portable", "zip"

; ── Mode detection (called from .onInit which is at end of file) ────────────
!macro DetectInstallMode
  ; The Package Foundry descriptor declares a user-scoped installer. Account
  ; membership must not silently redirect an unelevated install to Program Files
  ; or HKLM. A future system-wide artifact must use an explicit, separately
  ; validated elevation and deployment contract.
  StrCpy $install_mode "user"
!macroend

; ── Sections ─────────────────────────────────────────────────────────────────

Section "-Prerequisite" SecPrereq
  DetailPrint "Checking Visual C++ 2022 Redistributable..."
  IfFileExists "$WINDIR\System32\vcruntime140.dll" vc_installed vc_install
vc_install:
  DetailPrint "Downloading VC++ redistributable..."
  NSISdl::download "${VCREDIST_URL}" "$TEMP\vc_redist.x64.exe"
  Pop $0
  StrCmp $0 "success" vc_run
  DetailPrint "Download failed (HTTP $0) — continuing without VC++ redist"
  Goto vc_done
vc_run:
  DetailPrint "Installing VC++ redistributable (silent)..."
  ExecWait '"$TEMP\vc_redist.x64.exe" /install /quiet /norestart' $1
  DetailPrint "VC++ redist installer exit code: $1"
  Delete "$TEMP\vc_redist.x64.exe"
  Goto vc_done
vc_installed:
  DetailPrint "VC++ runtime already present"
vc_done:
SectionEnd

Section "Daemon" SecDaemon
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${BUILD_SRC}\wininspectd.exe"
  File "..\LICENSE"
  File "..\config.default.json"

  ; Uninstall registry
  StrCmp $install_mode "admin" reg_hklm reg_hkcu
reg_hkcu:
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayName" "WinInspect"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "Publisher" "Mark E. DeYoung"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "URLInfoAbout" "https://github.com/SemperSupra/WinInspect"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayIcon" "$INSTDIR\wininspectd.exe,0"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "NoRepair" 1
  Goto reg_done
reg_hklm:
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayName" "WinInspect"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "Publisher" "Mark E. DeYoung"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "URLInfoAbout" "https://github.com/SemperSupra/WinInspect"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayIcon" "$INSTDIR\wininspectd.exe,0"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "NoRepair" 1
reg_done:
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Write install.json with deployment and license metadata
  ; The daemon reads this on first startup and merges into config.json
  FileOpen $R4 "$INSTDIR\install.json" w
  FileWrite $R4 '{$\r$\n'
  FileWrite $R4 '  "deployment": "'
  ${If} $fleet_mode == "1"
    FileWrite $R4 'fleet'
  ${Else}
    FileWrite $R4 'interactive'
  ${EndIf}
  FileWrite $R4 '",$\r$\n'
  FileWrite $R4 '  "license": "'
  FileWrite $R4 $license_type
  FileWrite $R4 '"$\r$\n'
  FileWrite $R4 '}$\r$\n'
  FileClose $R4
  DetailPrint "Install metadata written (deployment=$fleet_mode, license=$license_type)"

  ; Event Log (admin only, not on Wine)
  ${If} $install_mode == "admin"
  ${If} $is_wine == "0"
    WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\EventLog\Application\WinInspect" \
      "EventMessageFile" "$INSTDIR\wininspectd.exe"
    WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Services\EventLog\Application\WinInspect" \
      "TypesSupported" 7
    DetailPrint "Event Log source registered"
  ${Else}
    DetailPrint "Event Log skipped (Wine)"
  ${EndIf}
  ${Else}
    DetailPrint "Event Log registration requires admin privileges"
  ${EndIf}

  ; Auto-start at login (fleet mode only, not on Wine)
  ${If} $fleet_mode == "1"
  ${If} $is_wine == "0"
    ${If} $install_mode == "admin"
      ; Admin installs can use Task Scheduler (requires admin rights)
      DetailPrint "Creating scheduled task: WinInspect service at user login..."
      nsExec::Exec 'schtasks /create /tn "WinInspect" /tr "$INSTDIR\wininspectd.exe --headless" /sc onlogon /ru CURRENTUSER /f'
      Pop $0
      DetailPrint "  schtasks exit code: $0"
      ${If} $0 != 0
        DetailPrint "  schtasks failed — falling back to HKCU Run key"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "WinInspectDaemon" "$INSTDIR\wininspectd.exe --headless"
      ${EndIf}
    ${Else}
      ; User installs: HKCU Run key (no admin needed)
      DetailPrint "Adding HKCU Run key for user auto-start..."
      WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "WinInspectDaemon" "$INSTDIR\wininspectd.exe --headless"
    ${EndIf}
  ${Else}
    DetailPrint "Auto-start skipped (Wine)"
  ${EndIf}
  ${EndIf}
SectionEnd

Section "CLI" SecCLI
  SetOutPath "$INSTDIR"
  File "${BUILD_SRC}\wininspect.exe"
SectionEnd

Section "GUI" SecGUI
  ${If} $fleet_mode != "1"
    SetOutPath "$INSTDIR"
    File "${BUILD_SRC}\wininspect-gui.exe"
  ${EndIf}
SectionEnd

Section "Start Menu Shortcuts" SecStartMenu
  ${If} $fleet_mode != "1"
    CreateDirectory "$SMPROGRAMS\WinInspect"
    CreateShortcut "$SMPROGRAMS\WinInspect\WinInspect GUI.lnk" "$INSTDIR\wininspect-gui.exe" "" "$INSTDIR\wininspect-gui.exe" 0
    CreateShortcut "$SMPROGRAMS\WinInspect\WinInspect CLI.lnk" "$INSTDIR\wininspect.exe" "" "$INSTDIR\wininspect-gui.exe" 0
    CreateShortcut "$SMPROGRAMS\WinInspect\Uninstall WinInspect.lnk" "$INSTDIR\uninstall.exe"
  ${EndIf}
SectionEnd

Section "Desktop Shortcut" SecDesktop
  ${If} $fleet_mode != "1"
    CreateShortcut "$DESKTOP\WinInspect GUI.lnk" "$INSTDIR\wininspect-gui.exe" "" "$INSTDIR\wininspect-gui.exe" 0
  ${EndIf}
SectionEnd

Section "Firewall Rule" SecFirewall
  ${If} $is_wine != "0"
    DetailPrint "Firewall rule skipped (Wine uses Linux firewall)"
  ${ElseIf} $install_mode == "admin"
    DetailPrint "Allowing WinInspect on private networks (TCP 1985)..."
    DetailPrint "  This is only needed for remote connections."
    DetailPrint "  Local connections (127.0.0.1) work without this."
    nsExec::Exec 'netsh advfirewall firewall add rule name="WinInspect (TCP 1985)" dir=in action=allow protocol=TCP localport=1985 description="WinInspect daemon RPC — window inspection for Windows and Wine" profile=private'
    Pop $0
    DetailPrint "  Exit code: $0"
  ${Else}
    DetailPrint "Firewall rule requires administrator privileges."
    DetailPrint "  Run the installer as Administrator to enable remote access."
    DetailPrint "  Local connections work without this."
  ${EndIf}
SectionEnd

; ── Uninstall ────────────────────────────────────────────────────────────────
; ── Uninstaller init (detect Wine for guarded cleanup) ───────────────────────
Function un.onInit
  StrCpy $is_wine "0"
  System::Call "kernel32::GetProcAddress(i kernel32::GetModuleHandle(ntdll), i 'wine_get_version') i .r0"
  ${If} $0 != 0
    StrCpy $is_wine "1"
  ${EndIf}
FunctionEnd

Section "Uninstall"
  MessageBox MB_YESNO|MB_ICONQUESTION \
    "Remove WinInspect configuration and instance identity?$\r$\n$\r$\n\
     Select YES to delete all settings,$\r$\n\
     Select NO to keep them for reinstallation." \
    /SD IDNO \
    IDNO skip_config
  RMDir /r "$APPDATA\WinInspect"
skip_config:
  Delete "$INSTDIR\wininspectd.exe"
  Delete "$INSTDIR\wininspect.exe"
  Delete "$INSTDIR\wininspect-gui.exe"
  Delete "$INSTDIR\config.default.json"
  Delete "$INSTDIR\install.json"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\WinInspect\WinInspect GUI.lnk"
  Delete "$SMPROGRAMS\WinInspect\WinInspect CLI.lnk"
  Delete "$SMPROGRAMS\WinInspect\Uninstall WinInspect.lnk"
  RMDir "$SMPROGRAMS\WinInspect"
  Delete "$DESKTOP\WinInspect GUI.lnk"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect"
  ; Clean up auto-start (HKCU Run key + schtasks, both safe to delete)
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "WinInspectDaemon"
  ${If} $is_wine == "0"
    DeleteRegKey HKLM "SYSTEM\CurrentControlSet\Services\EventLog\Application\WinInspect"
    nsExec::Exec 'netsh advfirewall firewall delete rule name="WinInspect (TCP 1985)"'
    nsExec::Exec 'schtasks /delete /tn "WinInspect" /f'
  ${EndIf}
SectionEnd

; ── Existing install detection ───────────────────────────────────────────────
Function DetectExistingInstall
  ; Sets:
  ;   $existing_method — "none" / "nsis" / "chocolatey" / "portable" / "zip"
  ;   $existing_version — version string from existing install ("" if none)
  ;
  StrCpy $existing_method "none"
  StrCpy $existing_version ""

  ; 1. Chocolatey-managed install
  ReadEnvStr $R0 "PROGRAMDATA"
  ${If} $R0 != ""
    StrCpy $R1 "$R0\chocolatey\lib\wininspect"
    IfFileExists "$R1\*.*" choco_detected skip_choco
  choco_detected:
    StrCpy $existing_method "chocolatey"
    ; Try to read version from chocolatey .nupkg or .version file
    ReadRegStr $existing_version HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayVersion"
    ${If} $existing_version == ""
      ReadRegStr $existing_version HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayVersion"
    ${EndIf}
    Return
  skip_choco:
  ${EndIf}

  ; 2. PortableApps.com install
  IfFileExists "$LOCALAPPDATA\WinInspectPortable\*.*" portable_detected skip_portable
portable_detected:
  StrCpy $existing_method "portable"
  Return
skip_portable:

  ; 3. NSIS/Chocolatey/winget install (via registry)
  ;    Check HKCU first (user install), then HKLM (admin install)
  ClearErrors
  ReadRegStr $existing_version HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayVersion"
  ${IfNot} ${Errors}
  ${AndIf} $existing_version != ""
    StrCpy $existing_method "nsis"
    Return
  ${EndIf}

  ClearErrors
  ReadRegStr $existing_version HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WinInspect" "DisplayVersion"
  ${IfNot} ${Errors}
  ${AndIf} $existing_version != ""
    StrCpy $existing_method "nsis"
    Return
  ${EndIf}

  ; 4. Portable ZIP install (binaries exist but no registry)
  IfFileExists "$LOCALAPPDATA\WinInspect\wininspectd.exe" zip_detected skip_zip
zip_detected:
  StrCpy $existing_method "zip"
  Return
skip_zip:

  ; Nothing found
  StrCpy $existing_method "none"
FunctionEnd

; ── Initialization (after sections so section constants are available) ───────
Function .onInit
  ; Scan command line for /FLEET and /INTERACTIVE.
  ; Default is interactive mode. /INTERACTIVE is explicit; /FLEET selects fleet.
  ; If both specified, /INTERACTIVE wins.
  StrCpy $fleet_mode "0"

  ; Scan for /FLEET
  StrLen $R0 $CMDLINE
  StrCpy $R1 0
fleet_loop:
  IntCmp $R1 $R0 fleet_done fleet_next fleet_done
fleet_next:
  StrCpy $R2 $CMDLINE 6 $R1
  ${If} $R2 == "/FLEET"
    StrCpy $fleet_mode "1"
    Goto fleet_done
  ${EndIf}
  IntOp $R1 $R1 + 1
  Goto fleet_loop
fleet_done:
  ClearErrors

  ; Scan for /INTERACTIVE (overrides /FLEET if both present)
  StrLen $R0 $CMDLINE
  StrCpy $R1 0
interactive_loop:
  IntCmp $R1 $R0 interactive_done interactive_next interactive_done
interactive_next:
  StrCpy $R2 $CMDLINE 9 $R1
  ${If} $R2 == "/INTERACTIVE"
    StrCpy $fleet_mode "0"
    Goto interactive_done
  ${EndIf}
  IntOp $R1 $R1 + 1
  Goto interactive_loop
interactive_done:
  ClearErrors

  ; Detect Wine — check for wine_get_version export in ntdll.dll
  StrCpy $is_wine "0"
  System::Call "kernel32::GetProcAddress(i kernel32::GetModuleHandle(ntdll), i 'wine_get_version') i .r1"
  ${If} $1 != 0
    StrCpy $is_wine "1"
  ${EndIf}

  ; Scan for /LICENSE-TYPE <value> (default: noncommercial)
  StrCpy $license_type "noncommercial"
  StrLen $R0 $CMDLINE
  StrCpy $R1 0
license_type_loop:
  IntCmp $R1 $R0 license_type_done license_type_next license_type_done
license_type_next:
  StrCpy $R2 $CMDLINE 12 $R1
  ${If} $R2 == "/LICENSE-TYPE"
    IntOp $R3 $R1 + 13
    StrCpy $R2 $CMDLINE 10 $R3
    ${If} $R2 == "commercial"
      StrCpy $license_type "commercial"
    ${EndIf}
    Goto license_type_done
  ${EndIf}
  IntOp $R1 $R1 + 1
  Goto license_type_loop
license_type_done:
  ClearErrors

  ; ── Log mode in silent mode ──────────────────────────────────────────
  ${If} ${Silent}
    ${If} $fleet_mode == "1"
      DetailPrint "Mode: Fleet (/FLEET)"
    ${Else}
      DetailPrint "Mode: Interactive (/INTERACTIVE or default)"
    ${EndIf}
  ${EndIf}

  ; ── Existing install detection ───────────────────────────────────────
  Call DetectExistingInstall
  ${If} $existing_method != "none"
    ; We found an existing install — decide what to do
    ${If} $existing_method == "chocolatey"
      ${If} ${Silent}
        DetailPrint "ABORT: Chocolatey-managed install detected — use 'choco upgrade wininspect' instead"
        Abort
      ${Else}
        StrCpy $R0 "This appears to be managed by Chocolatey.$\r$\n$\r$\nPlease use 'choco upgrade wininspect' to update,$\r$\nor uninstall with 'choco uninstall wininspect' first."
        MessageBox MB_OK|MB_ICONSTOP $R0 /SD IDOK
        Abort
      ${EndIf}

    ${ElseIf} $existing_method == "portable"
      ${IfNot} ${Silent}
        StrCpy $R0 "A PortableApps.com installation was detected at:$\r$\n$LOCALAPPDATA\WinInspectPortable$\r$\n$\r$\nThe portable version is self-contained and does not$\r$\nconflict with this installer. Both can coexist.$\r$\n$\r$\nClick OK to continue or CANCEL to abort."
        MessageBox MB_OKCANCEL|MB_ICONINFORMATION $R0 /SD IDOK IDOK portable_ok IDCANCEL portable_abort
portable_abort:
        Abort
portable_ok:
      ${EndIf}

    ${ElseIf} $existing_method == "zip"
      ${IfNot} ${Silent}
        StrCpy $R0 "A portable ZIP installation was detected at:$\r$\n$LOCALAPPDATA\WinInspect$\r$\n$\r$\nMixing installer and portable ZIP deployments is not$\r$\nrecommended. Consider removing the portable version$\r$\nfirst to avoid confusion.$\r$\n$\r$\nClick OK to continue or CANCEL to abort."
        MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION $R0 /SD IDOK IDOK zip_ok IDCANCEL zip_abort
zip_abort:
        Abort
zip_ok:
      ${EndIf}

    ${ElseIf} $existing_method == "nsis"
      ${If} $existing_version == "${VERSION}"
        ; Same version — reinstall
        ${IfNot} ${Silent}
          StrCpy $R0 "WinInspect ${VERSION} is already installed.$\r$\n$\r$\nWould you like to reinstall?"
          MessageBox MB_YESNO|MB_ICONQUESTION $R0 /SD IDNO IDYES nsis_continue_same IDNO nsis_abort_same
nsis_abort_same:
          Abort
nsis_continue_same:
        ${EndIf}
      ${Else}
        ; Different version — upgrade
        ${IfNot} ${Silent}
          StrCpy $R0 "Found WinInspect $existing_version.$\r$\n$\r$\nUpgrade to ${VERSION}?"
          MessageBox MB_YESNO|MB_ICONQUESTION $R0 /SD IDYES IDYES upgrade_ok IDNO upgrade_abort
upgrade_abort:
          Abort
upgrade_ok:
        ${EndIf}
      ${EndIf}
    ${EndIf}
  ${EndIf}

  !insertmacro DetectInstallMode
  SetShellVarContext current
  StrCpy $INSTDIR "$LOCALAPPDATA\WinInspect"
  ; Fleet mode overrides happen at the section level (see each section)
FunctionEnd

; ── License page pre-callbacks (skip the page that doesn't match $license_type) ─
Function preLicenseNC
  ${If} $license_type != "noncommercial"
    Abort
  ${EndIf}
FunctionEnd

Function preLicenseCommercial
  ${If} $license_type != "commercial"
    Abort
  ${EndIf}
FunctionEnd

; ── Component descriptions ───────────────────────────────────────────────────
LangString DESC_SecDaemon ${LANG_ENGLISH} "WinInspect daemon — Strix's engine. Required for all operations."
LangString DESC_SecCLI ${LANG_ENGLISH} "Command-line interface (wininspect.exe) — inspect and control Windows from the terminal."
LangString DESC_SecGUI ${LANG_ENGLISH} "Desktop companion (wininspect-gui.exe) — visual window inspection with Strix."
LangString DESC_SecStartMenu ${LANG_ENGLISH} "Start Menu shortcuts for quick access to WinInspect."
LangString DESC_SecDesktop ${LANG_ENGLISH} "Desktop shortcut for one-click launch of the WinInspect GUI."
LangString DESC_SecFirewall ${LANG_ENGLISH} "Windows Firewall rule for remote connections (TCP 1985). Required for remote daemon access. Administrator rights needed."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDaemon} $(DESC_SecDaemon)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCLI} $(DESC_SecCLI)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecGUI} $(DESC_SecGUI)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} $(DESC_SecStartMenu)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecFirewall} $(DESC_SecFirewall)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
