# Walkthrough Báo Cáo - Phase 8 OBS Source Catalog / PGM Playlist

Ngày lập: 2026-08-12

## Mục tiêu refactor

Playlist không còn nhận file path trực tiếp. `OBS Source Catalog` quản lý các source được Add Source trước đó; playlist chỉ lưu `sourceId` theo thứ tự.

```text
Add Source -> Source Catalog -> chọn source -> Add to Playlist -> PGM Playlist
```

Không sửa `InputManager`, `PlaybackManager`, `GlobalPlaylistController`, playlist legacy hoặc UI vận hành hiện hữu.

## Playlist mode PGM-only

Khi `Start Playlist`, current source được phát tại PGM. PVW giữ nguyên hoàn toàn: playlist không close, seek, pause, preload hoặc đổi media ở PVW.

`Next`, `Previous`, `Loop Playlist` và `Auto Next` chỉ thay source PGM. Không còn file picker multi-select trong flow playlist; source được thêm từng cái vào catalog giống input của mixer.

## Điều khiển

| Điều khiển | Hành vi |
| --- | --- |
| `Add Source` | Thêm một media vào catalog |
| `Remove Source` | Xóa source và reference của nó trong playlist |
| `Add to Playlist` | Thêm source đang chọn thành một step |
| `Remove Step`, `Move Up`, `Move Down` | Sửa thứ tự playlist |
| `Start Playlist`, `Stop Playlist` | Bật/tắt PGM-only mode |

Preload playlist được loại khỏi bản refactor để giữ đúng PGM-only. Nó chỉ được thêm lại sau khi source catalog/sequence và lifecycle PGM ổn định, dưới dạng backend preload riêng không dùng PVW.

## Checklist

1. Nhấn `Add Source` ba lần, chọn ba video khác nhau.
2. Chọn từng source, nhấn `Add to Playlist`, rồi thử Move Up/Down và Remove Step.
3. Đặt PVW ở media/position riêng; nhấn Start Playlist và Next/Previous.
4. Xác nhận PGM đổi source, PVW giữ media/position/state.
5. Test Auto Next và Loop tại EOF, sau đó Stop Playlist và kiểm tra TAKE/CUT/FADE manual.

Build Release: pass.
