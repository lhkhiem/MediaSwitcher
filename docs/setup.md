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

Script installer legacy đã được loại bỏ vì không đóng gói đúng cây runtime OBS. Chỉ tạo installer mới khi quy trình đóng gói giữ nguyên cấu trúc `bin/64bit`, `data` và `obs-plugins` của libobs.
