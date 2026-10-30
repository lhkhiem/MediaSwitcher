; MediaSwitcher Studio - Inno Setup 6
; Giữ nguyên cấu trúc runtime libobs: bin\64bit, data, obs-plugins.

#define AppName "MediaSwitcher"
#define AppFullName "MediaSwitcher Studio"
#define AppVersion "1.0.0"
#define AppPublisher "Lê Khiêm"
#define AppURL "https://lekhiem.io.vn"
#define AppExeName "MediaSwitcher.exe"
; Validated runtime baseline, also read by Build-Installer.ps1.
#define MinimumVCRedistVersion "14.50.35719.0"

#ifndef SourceRoot
  #define SourceRoot "..\build-obs\obs-runtime-stage"
#endif

#ifndef VCRedistPath
  #error Use Build-Installer.ps1 to supply the required x64 Visual C++ Redistributable.
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
MinVersion=10.0.17763
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

; Extract before installation so a failed prerequisite cannot look like success.
Source: "{#VCRedistPath}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: dontcopy
Source: "..\tools\CollectStartupDiagnostics.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppFullName}"; Filename: "{app}\bin\64bit\{#AppExeName}"; WorkingDir: "{app}\bin\64bit"; IconFilename: "{app}\bin\64bit\app_icon.ico"
Name: "{group}\Gỡ cài đặt {#AppFullName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppFullName}"; Filename: "{app}\bin\64bit\{#AppExeName}"; WorkingDir: "{app}\bin\64bit"; IconFilename: "{app}\bin\64bit\app_icon.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\64bit\{#AppExeName}"; WorkingDir: "{app}\bin\64bit"; Description: "Khởi chạy {#AppFullName}"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsVCRedist: Boolean;
var
  Installed: Cardinal;
  VersionText: String;
  InstalledVersion, RequiredVersion: Int64;
begin
  Result := True;
  if not RegQueryDWordValue(HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) then Exit;
  if Installed <> 1 then Exit;
  if not RegQueryStringValue(HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', VersionText) then Exit;
  if (Length(VersionText) > 0) and (Lowercase(Copy(VersionText, 1, 1)) = 'v') then
    Delete(VersionText, 1, 1);
  if not StrToVersion(VersionText, InstalledVersion) then Exit;
  if not StrToVersion('{#MinimumVCRedistVersion}', RequiredVersion) then Exit;
  Result := ComparePackedVersion(InstalledVersion, RequiredVersion) < 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  if not NeedsVCRedist then Exit;
  ExtractTemporaryFile('vc_redist.x64.exe');
  // Keep the app per-user; elevate only Microsoft's machine-wide prerequisite.
  if not ShellExec('runas', ExpandConstant('{tmp}\vc_redist.x64.exe'),
    '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Result := 'Không thể cài Microsoft Visual C++ Runtime: ' + SysErrorMessage(ResultCode);
    Exit;
  end;
  Log(Format('Visual C++ Redistributable exit code: %d', [ResultCode]));
  if (ResultCode = 3010) or (ResultCode = 1641) then
  begin
    NeedsRestart := True;
    Result := 'Vui lòng khởi động lại Windows rồi chạy lại bộ cài MediaSwitcher để hoàn tất cập nhật Visual C++ Runtime.';
    Exit;
  end;
  // 1638 is acceptable only if a sufficient version is now registered.
  if (ResultCode <> 0) and (ResultCode <> 1638) then
    Result := Format('Cài Microsoft Visual C++ Runtime thất bại (mã %d). Kiểm tra log trong thư mục %%TEMP%%.', [ResultCode])
  else if NeedsVCRedist then
    Result := 'Microsoft Visual C++ Runtime x64 chưa đạt phiên bản {#MinimumVCRedistVersion}. Không thể tiếp tục cài đặt.';
end;
