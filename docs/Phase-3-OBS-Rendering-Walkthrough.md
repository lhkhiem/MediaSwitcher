# Walkthrough Báo Cáo - Phase 3 OBS Rendering

Ngày lập: 2026-08-12  
Phạm vi: Ổn định đường render độc lập `obs_source_t -> obs_view_t -> obs_display_t -> QWidget` trong cửa sổ prototype. Chưa tích hợp PVW/PGM, `MainWindow`, transport legacy hay workflow TAKE/CUT/FADE.

## 1. Thay đổi triển khai

- `ObsMediaTestWindow` khởi tạo display sau khi Qt đã show native surface; lời gọi lặp lại là idempotent.
- Resize chỉ gửi tới `obs_display_resize` khi bề mặt có kích thước hợp lệ khác 0.
- Thay đổi trạng thái cửa sổ sẽ lên lịch resize sau event-loop tick, tránh dùng kích thước cũ khi vào hoặc thoát fullscreen.
- Thêm phím `F11` để vào/thoát fullscreen.
- Khi đóng cửa sổ, timer dừng trước; sau đó callback render, `obs_display_t`, source và `ObsContext` được giải phóng theo thứ tự đang có.

## 2. Audio activation cần cho render prototype

`obs_view_create()` tạo AUX view. AUX view render được hình nhưng không cấp active reference cho audio monitor WASAPI. `ObsPlaybackBackend` vì vậy tăng active reference cho source khi attach vào view và giảm reference trước khi release source.

Log diagnostics phải có:

```text
OBS media: Source activated for audio monitoring: active=true
OBS media: state=3 ... monitored=true sourceActive=true
```

## 3. Kết quả tự động

Build OBS Release thành công.

Smoke test đã xác nhận:

```text
OBS: Video initialized
OBS media: Test display created
sourceActive=true
```

Instance smoke test được đóng sau khi kiểm tra. `git diff --check` và xác thực UTF-8 được chạy sau thay đổi.

## 4. Kiểm thử thủ công

1. Bấm đúp `Run-OBS-Media-Test.cmd` và chọn một video.
2. Resize cửa sổ liên tục giữa kích thước nhỏ và lớn; hình phải tiếp tục render, không đen hoặc crash.
3. Nhấn `F11`, phát ít nhất 30 giây, rồi nhấn `F11` lần nữa; hình và tiếng phải tiếp tục.
4. Thử Play, Pause, Resume, `-10s` và `+10s` sau khi fullscreen.
5. Đóng cửa sổ bằng nút X; xác nhận không crash.

## 5. Giới hạn

Phase 3 chỉ xử lý bề mặt render prototype. Không có `MainWindow` OBS, PVW/PGM độc lập, TAKE, Quick Play, CUT, FADE, playlist, routing Program audio hoặc thay thế legacy engine. Các nội dung đó thuộc Phase 4 trở đi.
