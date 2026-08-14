# Walkthrough Bao Cao - Phase 1.5 Controlled OBS Runtime

Ngay lap: 2026-08-12
Pham vi: Chuan bi va kiem chung OBS Studio/libobs runtime Windows x64 co kiem soat. Khong trien khai media playback va khong thay the legacy playback.

## 1. Muc tieu

Phase 1.5 xac nhan MediaSwitcher co the build voi `MEDIASWITCHER_ENABLE_OBS=ON`, su dung mot runtime OBS da pin phien ban, va hoan tat vong doi libobs: startup, D3D11, audio core, nap `obs-ffmpeg`, shutdown.

Che do mac dinh van la `MEDIASWITCHER_ENABLE_OBS=OFF`; duong playback legacy khong bi thay doi.

## 2. Phien ban va nguon runtime

| Hang muc | Gia tri |
| --- | --- |
| OBS Studio/libobs | `32.1.2` |
| Kien truc | Windows x64 |
| Runtime artifact | `OBS-Studio-32.1.2-Windows-x64.zip` |
| SHA-256 runtime | `8d97e4563bd8d22d03e63042aa7dccede1d555c9bd35ce8a9e5019b0d0201bf6` |
| Source tag artifact | `obs-studio` tag `32.1.2` |
| SHA-256 source | `b4a59410cddb46d0e31df1ee13b8ec66f30862d7e980c1a8c4e3b5d16fae6053` |

Tat ca `obs.dll`, plugin, DLL phu thuoc, data va headers phai thuoc cung artifact/build `32.1.2`. CMake tu choi runtime khong co manifest dung version va `windows-x64`.

## 3. Runtime co kiem soat

Runtime duoc tao ngoai repository tai `D:\deps\obs-runtime`; binary OBS khong duoc commit.

```text
D:\deps\obs-runtime\
  include\                         libobs headers tu source tag 32.1.2
  lib\obs.lib                      tao tu export table cua obs.dll cung artifact
  lib\obs.def
  bin\64bit\
    obs.dll
    libobs-d3d11.dll
    avcodec-61.dll
    avdevice-61.dll
    avfilter-10.dll
    avformat-61.dll
    avutil-59.dll
    libx264-164.dll
    swresample-5.dll
    swscale-8.dll
    zlib.dll
    w32-pthreads.dll
    librist.dll
    srt.dll
  obs-plugins\64bit\obs-ffmpeg.dll
  data\libobs\                    effect files, bao gom default.effect
  data\obs-plugins\obs-ffmpeg\   plugin locale data
  runtime-manifest.json
```

`avfilter-10.dll` va `libx264-164.dll` la hai phu thuoc quan trong duoc phat hien trong qua trinh validation. Thieu mot trong hai se lam `obs-ffmpeg` khong nap duoc hoac runtime khong day du.

## 4. Thay doi code va CMake

- `FindObsRuntime.cmake` validate `OBS_RUNTIME_ROOT`, manifest, version, architecture, headers, import library, DLL, plugin va data bat buoc.
- `ObsContext` la chu so huu duy nhat cua lifecycle libobs.
- Khi ON, executable duoc deploy theo layout ma libobs Windows tu nhan dien:

```text
build-obs\obs-runtime-stage\
  bin\64bit\MediaSwitcher.exe
  obs-plugins\64bit\obs-ffmpeg.dll
  data\libobs\
  data\obs-plugins\obs-ffmpeg\
```

- `ObsContext` log runtime root, version, backend, ket qua video/audio, module da nap va shutdown.
- `ObsPlaybackBackend` chi la skeleton; chua duoc gan vao `PlaybackManager`.
- `build-obs/` duoc ignore de tranh commit binary/artifact.

Khong sua `FileSource`, `FFmpegDecoder`, `AudioEngine`, `PlaybackManager`, `InputManager`, TAKE, CUT, FADE, playlist hay UI.

## 5. Ket qua validation

Build ON thanh cong voi runtime `D:\deps\obs-runtime`.

```text
OBS: libobs startup succeeded. Version: 32.1.2.
OBS: Video initialized: 1920x1080 @ 60000/1001 FPS using libobs-d3d11.
OBS: Audio core initialized: 48000 Hz stereo.
OBS: Loaded module 'obs-ffmpeg.dll'.
OBS: Foundation initialized successfully. Loaded 1 module(s).
```

Smoke test lifecycle doc lap da xac nhan:

```text
obs-version=32.1.2
video-result=0
audio-result=1
module-failures=0 loaded-count=1 obs-ffmpeg=1
shutdown=complete
```

Build OFF cung thanh cong, xac nhan legacy MediaSwitcher van build binh thuong. `git diff --check` va kiem tra UTF-8 deu pass.

## 6. Lenh tai tao

```powershell
.\tools\PrepareObsRuntime.ps1 -RuntimeRoot D:\deps\obs-runtime

cmake -S . -B build-obs `
  -DMEDIASWITCHER_ENABLE_OBS=ON `
  -DOBS_RUNTIME_ROOT="D:\deps\obs-runtime" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"

cmake --build build-obs --config Release --parallel 4
.\build-obs\obs-runtime-stage\bin\64bit\MediaSwitcher.exe
```

Kiem tra legacy:

```powershell
cmake -S . -B build -DMEDIASWITCHER_ENABLE_OBS=OFF `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"
cmake --build build --config Release --parallel 4
git diff --check
```

## 7. Gioi han da biet

- `libobs-winrt` va AMF la module/tinh nang tuy chon, khong nam trong runtime toi thieu. Log co the thong bao khong tim thay chung, nhung `obs-ffmpeg` van nap thanh cong.
- `windeployqt` co the can chay ngoai sandbox vi no goi `qtpaths.exe` con.
- Shutdown cua libobs da duoc xac nhan bang smoke test. Dong cua so MediaSwitcher bang UI can kiem thu trong desktop session tuong tac.
- Chua co `obs_source_t`, Play/Pause/Seek, Preview/Program OBS view, audio routing/monitoring OBS hay bat ky Phase 2 nao.
