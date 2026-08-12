param(
    [switch]$Dual
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot 'build-obs\obs-runtime-stage\bin\64bit\MediaSwitcher.exe'

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Khong tìm thấy OBS test executable: $executable`nHãy build cấu hình build-obs trước khi chạy launcher này."
}

Add-Type -AssemblyName System.Windows.Forms
$dialog = New-Object System.Windows.Forms.OpenFileDialog
$dialog.Title = 'Chon media file cho MediaSwitcher OBS test'
$dialog.Filter = 'Media files|*.mp4;*.mkv;*.mov;*.avi;*.m4v;*.webm;*.mp3;*.wav;*.flac|All files|*.*'
$dialog.Multiselect = $false

if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
    Write-Host 'Đã hủy chọn media. Prototype OBS không được khởi động.'
    exit 0
}

$mode = if ($Dual) { '--obs-dual-media-test' } else { '--obs-media-test' }
$argument = '{0}="{1}"' -f $mode, $dialog.FileName
Write-Host "Đang khởi động OBS media test:`n$($dialog.FileName)"
Start-Process -FilePath $executable -ArgumentList $argument -WorkingDirectory (Split-Path -Parent $executable)
Write-Host 'Lệnh đã được gửi. Kiểm tra cửa sổ MediaSwitcher OBS Media Test.'
