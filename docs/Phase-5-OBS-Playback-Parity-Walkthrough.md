# Walkthrough Báo Cáo - Phase 5 OBS Playback Parity

Ngày lập: 2026-08-12  
Phạm vi: Hoàn thiện transport prototype OBS trước Phase 6. Không tích hợp vào `MainWindow`, TAKE, Quick Play, CUT, FADE, playlist hoặc legacy engine.

## 1. Transport có sẵn

`ObsPlaybackBackend` cung cấp:

```text
play / pause / stop / seek
position / duration / state
loop on/off
media_started / media_ended diagnostics
```

Seek, pause và stop vẫn flush monitor WASAPI trước để không phát PCM cũ. Chỉ instance có `audioOutput=true` mới tạo monitor và phát ra loa.

## 2. Loop và EOF

`setLooping(bool)` cập nhật setting `looping` của `ffmpeg_source` bằng `obs_source_update`.

Backend đăng ký trực tiếp signal libobs:

```text
media_started
media_ended
```

Khi EOF không loop, log có:

```text
OBS media: Source signalled media_ended. looping=false
```

Khi loop bật, source quay lại đầu file và tiếp tục phát; PVW/PGM không chia sẻ source hoặc clock.

## 3. Điều khiển test

`Run-OBS-Media-Test.cmd` có nút `Loop: Off/On`.

`Run-OBS-Dual-Media-Test.cmd` có loop riêng trên từng panel. PGM là panel có audio; PVW vẫn silent.

## 4. Kết quả tự động

Build OBS Release pass. Smoke log xác nhận:

```text
OBS media: Source signalled media_started.
OBS dual media: PVW state=4 position~90s | PGM state=3 position~30s.
```

`git diff --check` pass.

## 5. Checklist thủ công cần xác nhận

Chọn file ngắn, khoảng 10–30 giây, rồi thực hiện:

1. Chạy single media test, để `Loop: Off`, chờ EOF; hình phải clear/stopped và log có `media_ended`.
2. Bật `Loop: On`, chờ qua EOF ít nhất hai lần; video/audio phải quay đầu liên tục, không crash.
3. Tắt loop trong lúc phát; EOF kế tiếp phải dừng và báo `media_ended`.
4. Trong dual test, bật loop chỉ PVW: PGM phải tiếp tục position/audio không đổi.
5. Bật loop chỉ PGM: PVW phải vẫn pause ở position riêng.
6. Sau từng thao tác, thử Pause/Resume/Seek và đóng cửa sổ; không được còn audio cũ hoặc crash.

## 6. Giới hạn

Đây vẫn là prototype OBS tách biệt. Chưa có workflow PVW-to-PGM promotion hoặc các operation vận hành. Phase 6 mới triển khai TAKE, Quick Play và CUT sau khi checklist trên đạt.
