# Thiết lập và build MediaSwitcher (OBS-only)

MediaSwitcher chỉ sử dụng runtime `libobs` đã kiểm soát. Dự án không còn build hoặc đóng gói backend FFmpeg/XAudio2/DirectX legacy.

## 1. Chuẩn bị runtime OBS

Tạo runtime OBS 32.1.2 theo [obs-runtime-32.1.2.md](obs-runtime-32.1.2.md). Runtime mặc định trong môi trường phát triển hiện tại:

```text
D:\deps\obs-runtime
```

## 2. Cấu hình

```powershell
cmake -S . -B build-obs -A x64 -Thost=x64 `
  -DOBS_RUNTIME_ROOT="D:\deps\obs-runtime" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"
```

## 3. Build

```powershell
cmake --build build-obs --config Release --parallel 4
```

Hoặc chạy `Build-OBS.cmd` sau khi đã cấu hình. Artifact được tạo tại:

```text
build-obs\obs-runtime-stage\bin\64bit\MediaSwitcher.exe
```

`windeployqt` chạy tự động sau build. Trước khi chạy, cần có tối thiểu `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, `platforms\qwindows.dll`, `obs.dll`, plugin `obs-ffmpeg`, `obs-transitions` và `image-source` trong cây runtime stage.

## 4. Chạy

```powershell
.\Run-MediaSwitcher-OBS.cmd
```

## 5. Tạo bộ cài Inno Setup

Bộ cài mới giữ nguyên cấu trúc `bin/64bit`, `data` và `obs-plugins` của libobs. Chạy:

```powershell
.\installer\Build-Installer.ps1
```

Script sẽ build Release, kiểm tra đầy đủ dependency OBS/Qt, tìm Inno Setup 6 và Microsoft Visual C++ Redistributable, sau đó tạo file tại:

```text
installer\output\MediaSwitcher-Setup-v1.3.0-x64.exe
```

Dùng `-SkipBuild` chỉ khi `build-obs\obs-runtime-stage` vừa được build và đã kiểm tra. Bộ cài chỉ lấy dependency trong manifest; log, workspace, executable smoke test và DLL legacy còn sót trong thư mục build sẽ không được đóng gói.

Ứng dụng được cài theo người dùng vào `%LOCALAPPDATA%\Programs\MediaSwitcher`, vì vậy log và workspace vẫn có quyền ghi mà không cần chạy bằng quyền Administrator. Shortcut dùng thư mục `bin\64bit` làm working directory để libobs nạp đúng shader và module data.

## 6. Tương thích Windows và chẩn đoán khởi động

- Bộ cài yêu cầu Windows x64, tối thiểu Windows 10 1809 (build 17763), theo nền tảng Windows được Qt 6.11 hỗ trợ. Cần kiểm thử thực tế trên từng bản Windows mục tiêu; cấu hình này không thay thế kết quả kiểm thử.
- OBS dùng Direct3D 11 và cần driver GPU phù hợp. Không đổi sang OpenGL của Qt để xử lý lỗi khởi tạo đồ họa OBS.
- Mốc Visual C++ Redistributable x64 dùng cho bộ cài là **14.50.35719.0** (gói đi kèm Qt trên máy build). Đây là mốc triển khai đã chọn, không phải khẳng định mọi binary đều cần chính xác phiên bản này.
- Script đóng gói chọn redistributable theo **phiên bản file**, từ Qt hoặc Visual Studio, và từ chối gói cũ hơn mốc trên. Có thể truyền `-VCRedistPath 'C:\path\vc_redist.x64.exe'` với gói x64 chính thức của Microsoft.
- Bộ cài kiểm tra phiên bản runtime đã đăng ký, nâng cấp khi thiếu/cũ, yêu cầu UAC chỉ cho bước cài runtime hệ thống. Nếu bước này thất bại, bị hủy hoặc cần khởi động lại, bộ cài dừng với thông báo cụ thể.

Nếu ứng dụng không hiện cửa sổ, chạy PowerShell trên máy gặp lỗi:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\MediaSwitcher\tools\CollectStartupDiagnostics.ps1" -Launch
```

Nếu kiểm tra bộ cài cũ chưa chứa công cụ này, chép `tools\CollectStartupDiagnostics.ps1` từ mã nguồn sang máy Windows 10 rồi chạy script đó. Nếu cài ở đường dẫn khác, truyền `-InstallRoot 'D:\Apps\MediaSwitcher'`.

Script tạo báo cáo UTF-8 trong `%TEMP%\MediaSwitcher-startup-*.txt`, gồm phiên bản Windows, GPU, runtime, DLL, mã thoát, log gần nhất và sự kiện Windows liên quan. Báo cáo chỉ được lưu cục bộ. `-Launch` chạy ứng dụng với `--startup-smoke-test`: bản mới kiểm tra Qt, OBS, Direct3D và plugin rồi thoát; bản cũ chưa có cờ này sẽ khởi động như bình thường. Nếu tiến trình không thoát sau 15 giây, công cụ ghi nhận trạng thái và giữ nguyên tiến trình để kiểm tra tiếp. Không truyền `-Launch` nếu chỉ muốn thu thập dữ liệu sẵn có.

Log ứng dụng ở `bin\64bit\logs\mediaswitcher.log` khi mở từ shortcut. Log mới chứa cả thông báo Qt và libobs, gồm nguyên nhân nạp plugin/GPU thất bại. Lỗi Windows loader xảy ra trước `main()` vẫn cần đọc mã thoát hoặc Windows Event Log; thông báo trong ứng dụng không xử lý được trường hợp này.

Tham chiếu: [Qt 6.11 trên Windows](https://doc.qt.io/qt-6/windows.html), [yêu cầu OBS](https://obsproject.com/kb/system-requirements), [quy tắc phiên bản Visual C++ Runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170).
