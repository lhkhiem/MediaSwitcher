; MediaSwitcher Studio - Inno Setup 6
; Giữ nguyên cấu trúc runtime libobs: bin\64bit, data, obs-plugins.

#define AppName "MediaSwitcher"
#define AppFullName "MediaSwitcher Studio"
#define AppVersion "1.0.0"
#define AppPublisher "Lê Khiêm"
#define AppURL "https://lekhiem.io.vn"
#define AppExeName "MediaSwitcher.exe"

#ifndef SourceRoot
  #define SourceRoot "..\build-obs\obs-runtime-stage"
#endif

#ifndef VCRedistPath
  #define VCRedistPath ""
#endif

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#AppFullName}
AppVersion={#AppVersion}
AppVerName={#AppFullName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=Bộ cài đặt {#AppFullName}
VersionInfoProductName={#AppFullName}
VersionInfoProductVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppFullName}
DisableProgramGroupPage=yes
AllowNoIcons=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
OutputDir=output
OutputBaseFilename=MediaSwitcher-Setup-v{#AppVersion}-x64
SetupIconFile=..\src\app_icon.ico
UninstallDisplayName={#AppFullName}
UninstallDisplayIcon={app}\bin\64bit\{#AppExeName}
LicenseFile=..\COPYRIGHT
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
MinVersion=10.0
CloseApplications=force
RestartApplications=no
ChangesAssociations=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Tạo biểu tượng trên Desktop"; GroupDescription: "Biểu tượng bổ sung:"; Flags: unchecked

[Files]
; Ứng dụng
Source: "{#SourceRoot}\bin\64bit\{#AppExeName}"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\app_icon.ico"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion

; Qt runtime
Source: "{#SourceRoot}\bin\64bit\Qt6Core.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\Qt6Gui.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\Qt6Widgets.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\Qt6Network.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\Qt6Svg.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\opengl32sw.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\generic\*"; DestDir: "{app}\bin\64bit\generic"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\bin\64bit\iconengines\*"; DestDir: "{app}\bin\64bit\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\bin\64bit\imageformats\*"; DestDir: "{app}\bin\64bit\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\bin\64bit\networkinformation\*"; DestDir: "{app}\bin\64bit\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\bin\64bit\platforms\*"; DestDir: "{app}\bin\64bit\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\bin\64bit\styles\*"; DestDir: "{app}\bin\64bit\styles"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\bin\64bit\tls\*"; DestDir: "{app}\bin\64bit\tls"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\bin\64bit\translations\*.qm"; DestDir: "{app}\bin\64bit\translations"; Flags: ignoreversion

; libobs và dependency FFmpeg chính xác của runtime OBS 32.1.2
Source: "{#SourceRoot}\bin\64bit\obs.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\libobs-d3d11.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\avcodec-61.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\avdevice-61.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\avfilter-10.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\avformat-61.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\avutil-59.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\swresample-5.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\swscale-8.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\librist.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\libx264-164.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\srt.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\w32-pthreads.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\bin\64bit\zlib.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion

; Plugin và data phải giữ vị trí tương đối so với bin\64bit
Source: "{#SourceRoot}\obs-plugins\64bit\*.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\data\libobs\*"; DestDir: "{app}\data\libobs"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\data\obs-plugins\*"; DestDir: "{app}\data\obs-plugins"; Flags: ignoreversion recursesubdirs createallsubdirs

#if VCRedistPath != ""
Source: "{#VCRedistPath}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall
#endif

[Icons]
Name: "{group}\{#AppFullName}"; Filename: "{app}\bin\64bit\{#AppExeName}"; WorkingDir: "{app}\bin\64bit"; IconFilename: "{app}\bin\64bit\app_icon.ico"
Name: "{group}\Gỡ cài đặt {#AppFullName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppFullName}"; Filename: "{app}\bin\64bit\{#AppExeName}"; WorkingDir: "{app}\bin\64bit"; IconFilename: "{app}\bin\64bit\app_icon.ico"; Tasks: desktopicon

[Run]
#if VCRedistPath != ""
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Đang cài Microsoft Visual C++ Runtime..."; Flags: waituntilterminated runhidden; Check: NeedsVCRedist
#endif
Filename: "{app}\bin\64bit\{#AppExeName}"; WorkingDir: "{app}\bin\64bit"; Description: "Khởi chạy {#AppFullName}"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsVCRedist: Boolean;
var
  Installed: Cardinal;
begin
  Result := not (RegQueryDWordValue(
    HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
    'Installed',
    Installed) and (Installed = 1));
end;
