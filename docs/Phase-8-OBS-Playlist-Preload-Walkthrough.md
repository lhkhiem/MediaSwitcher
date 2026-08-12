# Walkthrough Báo Cáo - Phase 8 OBS Playlist / Preload

Ngày lập: 2026-08-12  
Phạm vi: Playlist và preload trong OBS dual PVW/PGM prototype. Không nối lại hoặc sửa playlist legacy.

## 1. Kiến trúc

`ObsPlaylist` là controller metadata nhỏ, chỉ giữ danh sách path, index hiện tại, Loop Playlist và Auto Next. Nó không tạo decoder hay source OBS.

Runtime giữ tối đa ba `ObsPlaybackBackend` active:

```text
1. Program: item hiện tại, có audio monitor
2. Preview: item kế, paused, không audio
3. Preload: item sau Preview, paused, không audio, không render
```

Đây là giới hạn `MAX_TOTAL_ACTIVE_PLAYBACKS = 3`. Khi bắt đầu FADE, preload bị release trước vì FADE tạm thời cần outgoing PGM, incoming PGM và PVW.

## 2. Điều khiển

| Điều khiển | Hành vi |
| --- | --- |
| `Load Playlist` | Mở dialog multi-select; giữ `Ctrl` hoặc `Shift` để chọn nhiều file, item đầu tiên vào PGM |
| `Previous` | Chuyển PGM tới item trước |
| `Next` | Chuyển PGM tới item sau |
| `Loop Playlist` | Cho phép quay vòng từ cuối về đầu và ngược lại |
| `Auto Next` | Khi PGM signal EOF, tự chuyển item kế |

Sau mỗi lần chuyển item, prototype dựng lại lookahead: PVW giữ item kế, preload giữ item sau PVW. Status hiển thị index playlist và `Preload: Ready/None`.

## 3. Chính sách tài nguyên

```text
PGM > PVW > Preload
```

Preload không có display và không phát âm thanh. Không preload tất cả file. Với playlist có một hoặc hai item, preload có thể là `None` để tránh duplicate instance không cần thiết.

## 4. Build

```powershell
cmake --build build-obs --config Release --parallel 1
```

Kết quả: build Release thành công.

## 5. Checklist kiểm thử thủ công

1. Chạy `Run-OBS-Dual-Media-Test.cmd` và chọn một media bất kỳ.
2. Nhấn `Load Playlist`, chọn tối thiểu ba file video.
3. Xác nhận PGM phát file 1, PVW paused file 2, status có `Playlist 1/3` và `Preload: Ready`.
4. Nhấn `Next`: xác nhận PGM là file 2, PVW là file 3; lặp lại với `Previous`.
5. Tắt `Loop Playlist`, đi đến cuối và nhấn `Next`: PGM không được wrap về đầu.
6. Bật `Loop Playlist`, đi đến cuối rồi `Next`: PGM phải quay về file đầu.
7. Bật `Auto Next`, dùng file ngắn hoặc seek PGM gần EOF: xác nhận PGM chuyển file tiếp theo đúng một lần.
8. Nhấn FADE trong playlist: preload phải được release trước; sau FADE UI không crash, không vượt ba instance active.
9. Đóng cửa sổ: xác nhận log release PGM/PVW/preload sạch và `obs_shutdown()` hoàn tất.

## 6. Giới hạn

Phase này chưa có editor playlist, persistence, reorder, transition-per-item hoặc audio crossfade playlist. `Next/Previous` dùng chuyển PGM ngay lập tức; transition type cho từng playlist item là phạm vi workflow kế tiếp, không phải Phase 8.
