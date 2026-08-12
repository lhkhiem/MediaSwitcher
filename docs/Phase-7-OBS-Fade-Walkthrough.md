# Walkthrough Báo Cáo - Phase 7 OBS FADE

Ngày lập: 2026-08-12  
Phạm vi: Bổ sung chuyển cảnh FADE cho prototype OBS dual PVW/PGM. Không tích hợp vào workflow legacy.

## 1. Cách triển khai

FADE dùng source type chính thức `fade_transition` của plugin `obs-transitions` thuộc OBS Studio/libobs `32.1.2`.

```text
PGM đang phát + PVW đã chọn
  -> tạo backend PGM mới tại asset/position của PVW
  -> tạo fade_transition của libobs
  -> obs_transition_set(outgoing PGM)
  -> obs_transition_start(incoming PGM, duration)
  -> obs_view của PGM render transition trong lúc FADE
  -> transition hoàn tất
  -> giải phóng outgoing PGM và transition
```

Không có frame decode/upload CPU và không dùng opacity/animation của Qt. Việc blend hai texture và nội suy hình ảnh do shader `fade_transition.effect` chính thức của OBS thực hiện.

## 2. Runtime kiểm soát

Runtime OBS `32.1.2` được mở rộng tối thiểu với hai file của chính artifact đã pin:

```text
obs-plugins\64bit\obs-transitions.dll
data\obs-plugins\obs-transitions\fade_transition.effect
```

`FindObsRuntime.cmake` sẽ fail rõ ràng nếu thiếu một trong hai file. `ObsContext` cũng xác nhận module `obs-transitions` đã nạp, cùng với `obs-ffmpeg`, trước khi chạy prototype.

## 3. Điều khiển prototype

Trong `Run-OBS-Dual-Media-Test.cmd`:

| Điều khiển | Hành vi |
| --- | --- |
| `FADE` hoặc `F` | Chuyển PVW sang PGM bằng FADE OBS |
| Danh sách thời lượng | Chọn `300`, `700`, `1000`, hoặc `1500 ms` |
| `TAKE`, `Quick Play`, `CUT` | Giữ nguyên hành vi Phase 6 |

Trong khi FADE đang chạy, các nút promotion bị khóa để không có hai transition cùng lúc. PVW được pause tại thời điểm thực hiện; PGM mới phát tại đúng position của PVW. Backend PGM cũ chỉ được giải phóng sau khi OBS báo transition hoàn tất.

Audio PGM được chuyển sang backend PGM mới ngay khi FADE bắt đầu để tránh phát đồng thời hai monitor WASAPI. Phase này xác nhận video FADE; audio crossfade chuyên dụng chưa nằm trong phạm vi.

`fade_transition` được render qua PGM view, còn audio đi qua monitor của backend PGM mới. UI ưu tiên signal `source_transition_video_stop` của libobs; nếu driver không chuyển tiếp signal này, UI dùng `obs_transition_get_time() >= 1.0` từ chính transition clock của libobs để hoàn tất. Không dùng timer UI để suy đoán thời điểm hoàn tất.

## 4. Build

```powershell
cmake -S . -B build-obs `
  -DMEDIASWITCHER_ENABLE_OBS=ON `
  -DOBS_RUNTIME_ROOT="D:\deps\obs-runtime" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"

cmake --build build-obs --config Release --parallel 1
```

Kết quả: build `Release` thành công. `git diff --check` pass; xác thực UTF-8 các file Phase 7 pass.

## 5. Kiểm thử thủ công

1. Chạy `Run-OBS-Dual-Media-Test.cmd`, chọn một file video.
2. Để PGM phát và đặt PVW tại một position khác, rồi pause PVW.
3. Chọn `700 ms`, nhấn `FADE` hoặc phím `F`.
4. Xác nhận khung PGM blend mượt từ hình cũ sang hình PVW; không có màn đen, flicker, swap PVW/PGM hoặc crash.
5. Xác nhận sau FADE: PVW vẫn paused; PGM mới vẫn play và seek độc lập bình thường.
6. Lặp lại với `300`, `1000` và `1500 ms`; sau mỗi lượt test `CUT`, `TAKE`, `Quick Play` để xác nhận Phase 6 không bị hồi quy.
7. Đóng cửa sổ; kiểm tra log có `OBS dual media: FADE completed` và shutdown sạch.

## 6. Giới hạn validation tự động

Build và deployment đã xác nhận `obs-transitions.dll` cùng effect được stage cạnh executable. Smoke test không có desktop/GPU tương tác trong session automation dừng tại `obs_reset_video` mã `-1`, trước khi module/UI FADE được tạo. Vì vậy chuyển cảnh hình ảnh cần xác nhận bằng checklist trên trong desktop session tương tác.

Không triển khai Playlist/Preload hay Phase 8.
