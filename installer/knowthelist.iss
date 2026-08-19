#ifndef AppVersion
#define AppVersion "0.0.0"
#endif

[Setup]
AppId={{B2B5A9BE-8E45-4EB4-8A8C-6CBB7B0D9A5D}
AppName=Knowthelist
AppVersion={#AppVersion}
AppPublisher=Knowthelist
AppPublisherURL=https://github.com/knowthelist/knowthelist
DefaultDirName={autopf}\Knowthelist
DefaultGroupName=Knowthelist
OutputDir=..
OutputBaseFilename=knowthelist-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
UninstallDisplayIcon={app}\knowthelist.exe
WizardStyle=modern

[Files]
Source: "..\knowthelist-{#AppVersion}-Windows\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Knowthelist"; Filename: "{app}\knowthelist.exe"
Name: "{autodesktop}\Knowthelist"; Filename: "{app}\knowthelist.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\knowthelist.exe"; Description: "Launch Knowthelist"; Flags: nowait postinstall skipifsilent
