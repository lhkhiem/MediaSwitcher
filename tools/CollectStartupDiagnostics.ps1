[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:LOCALAPPDATA 'Programs\MediaSwitcher'),
    [string]$OutputPath = (Join-Path $env:TEMP ('MediaSwitcher-startup-{0}.txt' -f (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'
$InstallRoot = [IO.Path]::GetFullPath($InstallRoot)
$binDirectory = Join-Path $InstallRoot 'bin\64bit'
$report = [Collections.Generic.List[string]]::new()
$report.Add('MediaSwitcher - Chẩn đoán khởi động')
$report.Add("Thời điểm: $(Get-Date -Format o)")
$report.Add("Thư mục cài đặt: $InstallRoot")

try {
    $os = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    # ProductName can still say Windows 10 on Windows 11; use the build as evidence.
    $report.Add("Windows: Build=$($os.CurrentBuildNumber).$($os.UBR); DisplayVersion=$($os.DisplayVersion); OS64Bit=$([Environment]::Is64BitOperatingSystem); RegistryProductName=$($os.ProductName)")
} catch { $report.Add("Không đọc được phiên bản Windows: $($_.Exception.Message)") }
try {
    foreach ($gpu in (Get-CimInstance Win32_VideoController)) {
        $report.Add("GPU: $($gpu.Name); Driver=$($gpu.DriverVersion); Status=$($gpu.Status)")
    }
} catch { $report.Add("Không đọc được thông tin GPU: $($_.Exception.Message)") }

# Read the native x64 registration even from a 32-bit PowerShell host.
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
try {
    $runtime = $registry.OpenSubKey('SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64')
    if ($runtime) {
        try { $report.Add("Visual C++ x64: Installed=$($runtime.GetValue('Installed')); Version=$($runtime.GetValue('Version'))") }
        finally { $runtime.Dispose() }
    } else { $report.Add('Visual C++ x64: Không có thông tin đăng ký.') }
} finally { $registry.Dispose() }

$systemDirectory = Join-Path $env:WINDIR 'System32'
if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
    $systemDirectory = Join-Path $env:WINDIR 'Sysnative'
}
$files = @(
    'MediaSwitcher.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
    'platforms\qwindows.dll', 'obs.dll', 'libobs-d3d11.dll',
    '..\..\obs-plugins\64bit\obs-ffmpeg.dll',
    '..\..\obs-plugins\64bit\obs-transitions.dll',
    '..\..\obs-plugins\64bit\image-source.dll'
) | ForEach-Object { Join-Path $binDirectory $_ }
$files += @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
    'vcruntime140.dll', 'vcruntime140_1.dll', 'icuuc.dll', 'd3d11.dll', 'd3dcompiler_47.dll') |
    ForEach-Object { Join-Path $systemDirectory $_ }
foreach ($file in $files) {
    if (Test-Path -LiteralPath $file -PathType Leaf) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($file)
        $report.Add("DLL/EXE: $file | $($info.FileVersion)")
    } else { $report.Add("THIẾU: $file") }
}

# Preserve the previous failure log before a launch (the app truncates its log).
$logPath = Join-Path $binDirectory 'logs\mediaswitcher.log'
function Add-AppLog([string]$label) {
    $report.Add("`r`n--- $label ---")
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        $report.Add((Get-Content -LiteralPath $logPath -Encoding UTF8 -Tail 250) -join "`r`n")
    } else { $report.Add('Chưa có log ứng dụng tại đường dẫn này.') }
}
Add-AppLog 'Log trước khi kiểm tra'

if ($Launch) {
    $oldQtDebug = $env:QT_DEBUG_PLUGINS
    try {
        $env:QT_DEBUG_PLUGINS = '1'
        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = Join-Path $binDirectory 'MediaSwitcher.exe'
        $startInfo.WorkingDirectory = $binDirectory
        $startInfo.Arguments = '--startup-smoke-test'
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $process = [Diagnostics.Process]::Start($startInfo)
        try {
            if ($process.WaitForExit(15000)) {
                $unsignedExit = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$process.ExitCode), 0)
                $report.Add(('Mã thoát: {0} (0x{1:X8})' -f $process.ExitCode, $unsignedExit))
            } else {
                $report.Add("Tiến trình vẫn chạy sau 15 giây: PID=$($process.Id). Chưa kết luận khởi động thành công; tiến trình được giữ nguyên.")
            }
        } finally { $process.Dispose() }
    } catch { $report.Add("Không chạy được ứng dụng: $($_.Exception.Message)") }
    finally { $env:QT_DEBUG_PLUGINS = $oldQtDebug }
    Add-AppLog 'Log sau khi kiểm tra'
}

try {
    $events = Get-WinEvent -FilterHashtable @{
        LogName = 'Application'; StartTime = (Get-Date).AddHours(-2); Id = @(1000, 1001, 1026, 33, 59)
    } -MaxEvents 100 -ErrorAction Stop | Where-Object { $_.Message -match 'MediaSwitcher' } | Select-Object -First 10
    foreach ($event in $events) { $report.Add("`r`nWindows Event $($event.Id) [$($event.TimeCreated)]:`r`n$($event.Message)") }
} catch { $report.Add("Windows Event Log không có sự kiện phù hợp hoặc không đọc được: $($_.Exception.Message)") }

[IO.File]::WriteAllLines([IO.Path]::GetFullPath($OutputPath), $report, [Text.UTF8Encoding]::new($true))
Write-Output "Đã lưu chẩn đoán: $OutputPath"
