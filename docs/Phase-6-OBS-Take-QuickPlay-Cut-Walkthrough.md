# Walkthrough Báo Cáo - Phase 6 OBS TAKE / Quick Play / CUT

Ngày lập: 2026-08-12  
Phạm vi: Kết nối operator semantics vào prototype dual OBS. Không tích hợp `MainWindow`, `InputManager`, legacy TAKE/CUT/Quick Play, FADE hoặc playlist.

## 1. Promotion semantics

Mỗi lệnh `TAKE`, `Quick Play` và `CUT` thực hiện cùng một promotion có kiểm soát:

```text
capture: PVW asset, position, loop setting
pause PVW
close old PGM runtime
create a new PGM runtime from captured asset
seek new PGM runtime to captured PVW position
play PGM
```

Đây không phải swap. PVW không nhận old PGM instance và giữ runtime/source riêng ở state Paused.

## 2. Audio

PGM mới luôn có `audioOutput=true`. Closing PGM cũ hủy monitor WASAPI cũ trước khi PGM mới bật monitor, nên không trộn hoặc phát PCM của Program trước đó. PVW vẫn silent.

## 3. Điều khiển dual prototype

`Run-OBS-Dual-Media-Test.cmd` có ba nút trung tâm:

| Lệnh | Phím | Kết quả |
| --- | --- | --- |
| `TAKE` | `T` | Promote PVW sang PGM |
| `Quick Play` | nút | Promote PVW sang PGM |
| `CUT` | `C` | Promote PVW sang PGM tức thời |

Sau mọi operation, invariant bắt buộc là:

```text
PGM = Playing + audio output
PVW = Paused + silent
```

## 4. Kết quả build

OBS Release build pass. `git diff --check` và xác thực UTF-8 được chạy sau thay đổi.

## 5. Checklist thủ công

1. Mở dual test và để PGM chạy, PVW pause ở mốc 90 giây.
2. Seek PVW tới mốc dễ nhận biết, ví dụ 01:15, giữ PVW pause.
3. Nhấn `TAKE`: PGM phải chuyển sang mốc PVW, phát hình/tiếng; PVW vẫn đứng tại mốc đó.
4. Seek PVW tới mốc khác, nhấn `Quick Play`; xác nhận invariant tương tự.
5. Lặp lại bằng `CUT` và kiểm tra không còn audio PGM cũ.
6. Sau mỗi lệnh, thử thao tác PVW; PGM không được thay đổi.
7. Đóng cửa sổ; không crash hoặc audio còn phát.

## 6. Giới hạn

Phase 6 chỉ chứng minh semantics trên prototype dual OBS. Chưa sửa workflow vận hành thật trong `MainWindow` và chưa có FADE. Phase 7 mới xử lý FADE.
