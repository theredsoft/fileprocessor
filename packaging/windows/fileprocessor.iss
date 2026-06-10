; Inno Setup 6 — установщик FileProcessor.
; Запускается из build.ps1 -Installer; ожидает готовый dist\ рядом.

#define AppName "FileProcessor"
#define AppVersion "0.1.0"
#define AppPublisher "andreipanasyuk"

[Setup]
AppId={{7A1F6B7E-2C3D-4E8A-9B5F-3D2C1B0A9F8E}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=output
OutputBaseFilename=fileprocessor-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "dist\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\fileprocessor.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\fileprocessor.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Run]
Filename: "{app}\fileprocessor.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
