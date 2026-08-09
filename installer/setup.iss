; ============================================================
;  MediaSwitcher - Inno Setup Script
;  Tac gia : Le Khiem (lekhiem.io.vn)
;  Mo ta   : Tao file cai dat EXE cho MediaSwitcher
; ============================================================

#define AppName      "MediaSwitcher"
#define AppFullName  "MediaSwitcher Studio"
#define AppVersion   "1.0.0"
#define AppPublisher "Le Khiem"
#define AppURL       "https://lekhiem.io.vn"
#define AppExeName   "MediaSwitcher.exe"
#define SrcDir       "..\build\bin\Release"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#AppFullName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppFullName}
AllowNoIcons=yes
LicenseFile=..\COPYRIGHT
OutputDir=output
OutputBaseFilename=MediaSwitcher-Setup-v{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#AppFullName}
UninstallDisplayIcon={app}\{#AppExeName}
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Tao bieu tuong tren Desktop"; GroupDescription: "Bieu tuong:"; Flags: unchecked

[Files]
; --- Main Executable ---
Source: "{#SrcDir}\{#AppExeName}";          DestDir: "{app}"; Flags: ignoreversion

; --- Qt6 Core DLLs ---
Source: "{#SrcDir}\Qt6Core.dll";            DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\Qt6Gui.dll";             DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\Qt6Widgets.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\Qt6Network.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\Qt6Svg.dll";             DestDir: "{app}"; Flags: ignoreversion

; --- DirectX / OpenGL ---
Source: "{#SrcDir}\opengl32sw.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\dxcompiler.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\dxil.dll";               DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\D3DCOMPILER_47.dll";     DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; --- FFmpeg DLLs ---
Source: "{#SrcDir}\avcodec-63.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\avformat-63.dll";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\avutil-61.dll";          DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\swscale-10.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\swresample-7.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\avfilter-12.dll";        DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\avdevice-63.dll";        DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; --- Qt Plugins ---
Source: "{#SrcDir}\platforms\*";            DestDir: "{app}\platforms";          Flags: ignoreversion recursesubdirs
Source: "{#SrcDir}\styles\*";               DestDir: "{app}\styles";             Flags: ignoreversion recursesubdirs
Source: "{#SrcDir}\imageformats\*";         DestDir: "{app}\imageformats";       Flags: ignoreversion recursesubdirs
Source: "{#SrcDir}\iconengines\*";          DestDir: "{app}\iconengines";        Flags: ignoreversion recursesubdirs
Source: "{#SrcDir}\networkinformation\*";   DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs
Source: "{#SrcDir}\tls\*";                  DestDir: "{app}\tls";                Flags: ignoreversion recursesubdirs
Source: "{#SrcDir}\generic\*";              DestDir: "{app}\generic";            Flags: ignoreversion recursesubdirs

; --- Qt Multimedia Plugins ---
Source: "{#SrcDir}\plugins\multimedia\*";   DestDir: "{app}\plugins\multimedia"; Flags: ignoreversion skipifsourcedoesntexist

; --- Qt Translations ---
Source: "{#SrcDir}\translations\*";         DestDir: "{app}\translations";       Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#AppFullName}";         Filename: "{app}\{#AppExeName}"
Name: "{group}\Go cai dat";             Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppFullName}";   Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Khoi chay {#AppFullName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
