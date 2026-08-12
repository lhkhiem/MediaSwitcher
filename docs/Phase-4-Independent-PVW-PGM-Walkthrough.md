# Walkthrough Báo Cáo - Phase 4 Independent PVW / PGM

Ngày lập: 2026-08-12  
Phạm vi: Prototype OBS độc lập với hai playback runtime cho cùng một media asset. Không tích hợp `MainWindow`, `InputManager`, TAKE, Quick Play, CUT, FADE, playlist hoặc legacy renderer.

## 1. Kiến trúc triển khai

```text
same media file
    ├─ ObsPlaybackBackend PVW
    │     ├─ obs_source_t
    │     ├─ obs_view_t
    │     └─ obs_display_t -> Qt surface PVW
    └─ ObsPlaybackBackend PGM
          ├─ obs_source_t
          ├─ obs_view_t
          └─ obs_display_t -> Qt surface PGM
```

Mỗi backend sở hữu source, view, media clock và transport riêng. Không chia sẻ `obs_source_t`; vì vậy cùng một file có thể ở hai vị trí và state khác nhau.

## 2. Audio routing prototype

- PVW: audio output tắt, không tạo WASAPI monitor.
- PGM: audio output bật, là instance duy nhất tạo WASAPI monitor.

Điều này tránh trộn tiếng Preview vào Program trong khi vẫn giữ hai instance video độc lập.

## 3. State khởi tạo kiểm thử

Dual test tự yêu cầu state sau khi source sẵn sàng:

```text
PVW = 90 giây, Paused
PGM = 30 giây, Playing, audio output enabled
```

Nếu duration ngắn hơn, backend tự clamp seek trong duration thực tế.

## 4. Cách chạy

Bấm đúp `Run-OBS-Dual-Media-Test.cmd`, chọn một media dài hơn 90 giây.

Phím tắt:

| Phím | Tác vụ |
| --- | --- |
| `Space` | Play/Pause PVW |
| `Enter` | Play/Pause PGM |
| `F11` | Vào/thoát fullscreen dual test |

Mỗi panel cũng có Play/Pause, `-10s`, `+10s` và slider riêng.

## 5. Kết quả tự động

Build OBS Release thành công. Smoke test xác nhận:

```text
Startup mode: OBS dual media test
OBS dual media: Display created.  (2 lần)
OBS dual media: PVW state=4 ... | PGM state=3 ...
```

`state=4` là Paused và `state=3` là Playing.

## 6. Giới hạn

Phase 4 chỉ xác nhận independence của runtime instance. Chưa có promotion PVW sang PGM, transport parity đầy đủ, loop/EOF parity, TAKE, CUT, FADE hoặc workflow vận hành trong `MainWindow`. Các phần đó thuộc Phase 5 trở đi.
