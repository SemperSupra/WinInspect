#ifndef AppVersion
  #define AppVersion "dev"
#endif
#ifndef PayloadX64
  #error PayloadX64 is required
#endif
#ifndef PayloadArm64
  #error PayloadArm64 is required
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId=WinInspect
AppName=WinInspect
AppVersion={#AppVersion}
AppPublisher=Mark E. DeYoung
AppPublisherURL=https://github.com/SemperSupra/WinInspect
DefaultDirName={localappdata}\WinInspect
DefaultGroupName=WinInspect
OutputDir={#OutputDir}
OutputBaseFilename=WinInspect-Inno-{#AppVersion}
PrivilegesRequired=lowest
; Keep the default x86 Setup engine. It runs on x64 Windows and Arm64 Windows
; through built-in Windows emulation while carrying architecture-native payloads.
; This also keeps the experiment compatible with supported Inno Setup 6.x and 7.x.
ArchitecturesAllowed=win64
ArchitecturesInstallIn64BitMode=win64
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
Uninstallable=yes
UninstallDisplayName=WinInspect
UninstallDisplayIcon={app}\wininspect-gui.exe
DisableProgramGroupPage=yes
WizardStyle=modern

[Files]
Source: "{#PayloadX64}\wininspectd.exe"; DestDir: "{app}"; DestName: "wininspectd.exe"; Flags: ignoreversion; Check: not IsArm64
Source: "{#PayloadX64}\wininspect.exe"; DestDir: "{app}"; DestName: "wininspect.exe"; Flags: ignoreversion; Check: not IsArm64
Source: "{#PayloadX64}\wininspect-gui.exe"; DestDir: "{app}"; DestName: "wininspect-gui.exe"; Flags: ignoreversion; Check: not IsArm64
Source: "{#PayloadArm64}\wininspectd.exe"; DestDir: "{app}"; DestName: "wininspectd.exe"; Flags: ignoreversion; Check: IsArm64
Source: "{#PayloadArm64}\wininspect.exe"; DestDir: "{app}"; DestName: "wininspect.exe"; Flags: ignoreversion; Check: IsArm64
Source: "{#PayloadArm64}\wininspect-gui.exe"; DestDir: "{app}"; DestName: "wininspect-gui.exe"; Flags: ignoreversion; Check: IsArm64
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\config.default.json"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\WinInspect GUI"; Filename: "{app}\wininspect-gui.exe"
Name: "{group}\WinInspect CLI"; Filename: "{app}\wininspect.exe"
Name: "{group}\Uninstall WinInspect"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\wininspect-gui.exe"; Description: "Launch WinInspect GUI"; Flags: nowait postinstall skipifsilent
