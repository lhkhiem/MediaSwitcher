# Walkthrough Báo Cáo - Phase 8.5 OBS UI theo tổ chức vMix

Ngày lập: 2026-08-12

## Bố cục mới

`ObsDualMediaTestWindow` chỉ là UI prototype OBS; UI MediaSwitcher legacy không bị sửa.

```text
PVW                 TAKE / CUT / FADE                 PGM

INPUTS: [thumbnail input] [thumbnail input] ...       Add Input | Remove | Playlist
```

Input bank nằm dưới switcher và hiển thị source dưới dạng tile ngang: thumbnail 16:9, ID, tên file. Click một tile luôn nạp source vào PVW, kể cả khi playlist đang chạy trên PGM. QUICK PLAY, CUT hoặc FADE sẽ dừng playlist trước khi chuyển source theo workflow bình thường. Không tạo OBS playback instance cho từng tile.

## Add Input và thumbnail

`Add Input` thêm một file vào `ObsSourceCatalog` với ID ổn định. Mỗi source chỉ là metadata; thumbnail được tạo bất đồng bộ bằng `ThumbnailGenerator` hiện có ở kích thước 320x180, sau đó worker giải phóng decoder. Trong lúc thumbnail chưa sẵn sàng, tile hiển thị icon file.

Điều này giữ input bank nhẹ: thêm nhiều source không đồng nghĩa tạo nhiều decoder, audio queue hay OBS source playback.

## Playlist manager

Nút `Playlist` mở cửa sổ riêng, mô phỏng tổ chức vMix:

```text
Available Inputs  ->  PGM Playlist
```

Chọn source ở cột trái rồi nhấn `>` để đưa vào sequence. Có Remove, Up, Down, Loop Playlist, Auto Next, Save và Cancel. Sau khi lưu, thanh INPUTS hiển thị `Play Playlist`; khi đang chạy, nút đổi thành `Stop Playlist` và hai nút Previous/Next chỉ điều khiển playlist.

Playlist chỉ lưu `sourceId`; không có file picker trong playlist. Khi chạy, playlist chỉ đổi PGM. PVW không bị playlist close, seek, pause, preload hoặc đổi source.

## Kiểm thử

1. Chạy `Run-OBS-Dual-Media-Test.cmd`.
2. Nhấn `Add Input` vài lần để thêm video. Chờ tile thumbnail xuất hiện ở INPUTS.
3. Click tile để nạp PVW, xác nhận PGM không đổi.
4. Mở `Playlist`, thêm các source từ Available Inputs vào sequence và thử Up/Down/Remove.
5. Start Playlist, xác nhận Next/Previous/Auto Next chỉ đổi PGM; PVW độc lập.
6. Stop Playlist, xác nhận TAKE/CUT/FADE manual vẫn hoạt động.

## Build

```powershell
cmake --build build-obs --config Release --parallel 1
```

Kết quả build Release: pass.
