# Walkthrough Báo Cáo - Phase 2 OBS Media Playback Prototype

Ngày lập: 2026-08-12  
Phạm vi: Prototype phát một media file duy nhất bằng OBS/libobs. Không tích hợp PVW/PGM, `PlaybackManager`, `InputManager`, renderer legacy, XAudio2 legacy hay workflow TAKE/CUT/FADE.

## 1. Mục tiêu đã triển khai

Khi chạy cờ dòng lệnh dưới đây, MediaSwitcher đi vào chế độ test độc lập:

```powershell
MediaSwitcher.exe --obs-media-test="D:\Videos\test.mp4"
```

Luồng thực thi:

```text
media file
  -> obs_source_t("ffmpeg_source")
  -> obs_view_t
  -> obs_display_t
  -> QWidget test riêng

media audio
  -> libobs audio core
  -> WASAPI audio monitoring mặc định
```

Chế độ này không khởi tạo `AudioEngine`/XAudio2 và không tạo `MainWindow` legacy. Khi không có cờ, ứng dụng chạy luồng MediaSwitcher hiện hữu như trước.

## 2. Source và settings OBS 32.1.2

Source type: `ffmpeg_source`, do module `obs-ffmpeg.dll` đăng ký.

Settings đã đối chiếu từ source tag OBS 32.1.2:

| Setting | Giá trị prototype | Mục đích |
| --- | --- | --- |
| `is_local_file` | `true` | Phát file local |
| `local_file` | Absolute path UTF-8 | Hỗ trợ tên file Windows có tiếng Việt |
| `looping` | `false` | Cho phép kiểm tra EOF |
| `restart_on_activate` | `false` | Transport điều khiển tường minh |
| `clear_on_media_end` | `true` | Xóa frame tại EOF |

Phát hiện quan trọng: không truyền `std::filesystem::path::string()` cho file có ký tự tiếng Việt, vì conversion theo code page có thể làm tiến trình crash. Prototype chuyển `std::filesystem::path::wstring()` sang UTF-8 bằng Qt trước khi gọi `obs_data_set_string`.

## 3. Transport API và state

`ObsPlaybackBackend` sở hữu đúng một `obs_source_t` và cung cấp:

```cpp
bool open(const std::filesystem::path& path);
void play();
void pause();
void stop();
bool seekMs(int64_t milliseconds);
int64_t positionMs() const;
int64_t durationMs() const;
ObsPlaybackState state() const;
```

Transport dùng API libobs trực tiếp:

```text
obs_source_media_play_pause
obs_source_media_stop
obs_source_media_set_time
obs_source_media_get_time
obs_source_media_get_duration
obs_source_media_get_state
```

State mapping:

```text
NONE -> None
OPENING -> Opening
BUFFERING -> Buffering
PLAYING -> Playing
PAUSED -> Paused
STOPPED -> Stopped
ENDED -> Ended
unknown -> Error
```

## 4. Rendering và điều khiển test

`ObsMediaTestWindow` là cửa sổ Qt tách biệt, không sửa UI vận hành hiện hữu.

```text
obs_source_t -> obs_view_t -> obs_display_t -> QWidget native HWND
```

Điều khiển:

| Lệnh | Thao tác |
| --- | --- |
| `Space` hoặc nút Play/Pause | Pause hoặc resume |
| `Left` hoặc `-10s` | Seek lùi 10 giây |
| `Right` hoặc `+10s` | Seek tới 10 giây |
| `S` hoặc Stop | Stop |
| Slider | Seek theo duration |

Display callback chỉ render `obs_view_t`; không đọc raw frame và không upload frame vào renderer legacy.

## 5. Audio và ownership

Audio source được đặt `OBS_MONITORING_TYPE_MONITOR_ONLY`. libobs dùng WASAPI monitoring device mặc định (`Default`/`default`) để phát ra loa. Không dùng callback PCM, custom buffer, custom clock hoặc XAudio2.

Ownership được teardown theo thứ tự:

```text
obs_display_remove_draw_callback
obs_display_destroy
obs_view_set_source(view, 0, nullptr)
obs_view_destroy
signal_handler_disconnect(media_started/media_ended)
obs_source_release
ObsContext::obs_shutdown
```

Callbacks `media_started` và `media_ended` đã được đăng ký để diagnostics và EOF.

## 6. Kết quả build và runtime

Build đã pass với cả hai cấu hình:

```text
MEDIASWITCHER_ENABLE_OBS=ON  -> pass
MEDIASWITCHER_ENABLE_OBS=OFF -> pass
```

`git diff --check` và xác thực UTF-8 đều pass.

Đã chạy với đúng video dài từng phơi lộ lỗi sync của legacy engine:

```text
Nhạc Test Loa ... (1080p, h264).mp4
```

Runtime log xác nhận:

```text
OBS: libobs startup succeeded. Version: 32.1.2.
OBS media: Created 'ffmpeg_source' source.
OBS media: Enabled libobs WASAPI monitoring to the default output device.
OBS media: Test display created.
OBS media: state=3 position=202 ms duration=3944877 ms monitored=true
OBS media: state=3 position=5217 ms duration=3944877 ms monitored=true
OBS media: state=3 position=10303 ms duration=3944877 ms monitored=true
```

Duration `3,944,877 ms` tương đương khoảng 01:05:44, đúng với file test dài.

## 7. Các file thay đổi trong Phase 2

- `src/engine/obs/ObsPlaybackBackend.h`
- `src/engine/obs/ObsPlaybackBackend.cpp`
- `src/ui/ObsMediaTestWindow.h`
- `src/ui/ObsMediaTestWindow.cpp`
- `src/main.cpp`
- `src/CMakeLists.txt`

Không sửa `PlaybackManager`, `InputManager`, `FileSource`, `FFmpegDecoder`, `AudioEngine`, legacy Renderer, TAKE, Quick Play, CUT, FADE, playlist hay semantics PVW/PGM.

## 8. Lệnh tái tạo và kiểm thử thủ công

```powershell
cmake -S . -B build-obs `
  -DMEDIASWITCHER_ENABLE_OBS=ON `
  -DOBS_RUNTIME_ROOT="D:\deps\obs-runtime" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"

cmake --build build-obs --config Release --parallel 4

.\build-obs\obs-runtime-stage\bin\64bit\MediaSwitcher.exe `
  --obs-media-test="D:\Users\hoang\Downloads\Nhạc Test Loa Siêu Hay Nghe Ngọt Lịm Đôi Tai - LK Rumba Tuyển Chọn Những Bản Nhạc Không Lời Hay Nhất - Nhạc Sống Thanh Ngân (1080p, h264).mp4"
```

Checklist kiểm thử trực tiếp:

1. Phát liên tục ít nhất 2 phút.
2. Pause, chờ 3–5 giây, rồi resume.
3. Seek tới và lùi 10 giây nhiều lần.
4. Quan sát môi và tiếng, audio tail sau pause, video rollback sau resume.
5. Chạy đến EOF nếu dùng media ngắn; xác nhận log `media_ended`.
6. Đóng cửa sổ test bình thường; xác nhận không crash.

## 9. Giới hạn và bước tiếp theo

Automation đã xác nhận source creation, D3D11/libobs display creation, WASAPI monitoring enable, duration và position tăng đúng. Automation không thể thay thế quan sát hình/tiếng trong desktop session tương tác; vì vậy chưa được kết luận A/V sync ổn định cho tới khi checklist thủ công hoàn tất.

Chưa chuyển sang Phase 3. Sau khi test trực tiếp đạt, bước hợp lý tiếp theo là ổn định rendering lifecycle/resize/fullscreen của cửa sổ test trước khi chạm PVW/PGM độc lập.