# Tối ưu RAM cho MediaSwitcher – Multi-Source Loading

## Tóm tắt vấn đề

Khi load nhiều source (đặc biệt video chất lượng cao 4K/1080p), mỗi `FileSource` đang:
1. Mở một `FFmpegDecoder` riêng → mỗi source có codec context + packet queues riêng
2. Chạy một `decode worker thread` riêng liên tục decode frame (kể cả source không ai xem)
3. `FramePool` không có giới hạn kích thước → accumulate frames vô hạn
4. Packet queue video cho phép tích đến **100 packets** (`maxCount = 100`) — với 4K mỗi packet có thể vài trăm KB → 1 source có thể chiếm hàng chục MB chỉ trong packet queue
5. Audio packet queue **không giới hạn** (`"Audio queue is allowed to grow freely"`)

---

## Phân tích chi tiết nguyên nhân

| Nguồn RAM | Mô tả | Mức độ |
|---|---|---|
| **Video packet queue** | Mỗi source buffer đến 100 video packets. 4K ProRes ≈ 500KB/packet → 50MB/source | 🔴 Rất cao |
| **Audio packet queue** | Không có giới hạn trên, grow freely | 🟠 Cao |
| **FramePool không giới hạn** | Frames được pool nhưng không bao giờ clear | 🟡 Trung bình |
| **Tất cả source đều decode** | Source không được xem (không phải PVW/PGM) vẫn chạy full decode loop | 🔴 Rất cao |
| **Thread cho mỗi source** | Thread stack + overhead nhỏ nhưng cộng dồn | 🟡 Trung bình |
| **AVFrame buffers** | 3 AVFrame/source (m_avFrame, m_audioFrame, m_rgbaFrame) chứa uncompressed | 🟠 Cao |

---

## Chiến lược tối ưu đề xuất

### Chiến lược 1: **Lazy Decode / Smart Throttle** (ưu tiên cao, ít rủi ro)
Chỉ decode full-rate khi source đang là **PGM hoặc PVW**. Source khác ("idle") decode cực chậm (1 frame mỗi vài giây để giữ thumbnail preview) hoặc dừng hẳn worker.

### Chiến lược 2: **Giới hạn Packet Queue** (ưu tiên cao)
- Video packet queue: giảm từ 100 → **15–20 packets** 
- Audio packet queue: thêm giới hạn **100–200 packets** tránh grow vô hạn

### Chiến lược 3: **FramePool giới hạn kích thước** (ưu tiên trung bình)
- `FramePool` chỉ giữ tối đa **2–3 frame** (đủ để double-buffer)
- Frame vượt quá → drop thay vì pool

### Chiến lược 4: **Giảm Thread Counts của codec** (ưu tiên thấp)
- Source không phải PGM/PVW: giảm `thread_count` từ 4 → 1–2

---

## Open Questions

> [!IMPORTANT]
> **Câu hỏi quan trọng về UX**: Khi một source là "idle" (không phải PVW/PGM), hành vi mong muốn là gì?
> - **Option A** – Pause hoàn toàn: source dừng decode, vẫn giữ thumbnail tĩnh. Khi switch sang preview, resume từ vị trí đã pause. *(Tiết kiệm RAM nhất)*
> - **Option B** – Slow preview: source vẫn chạy nhưng decode chậm (vd 1fps), tiêu tốn ít RAM hơn nhưng vẫn "alive". Khi switch thì đã gần vị trí đúng.
> - **Option C** – Giữ nguyên behavior hiện tại nhưng chỉ giảm buffer size

> [!WARNING]
> Nếu chọn **Option A** (Pause idle sources): khi switch source từ idle sang PVW, sẽ có độ trễ nhỏ (~100-300ms) để resume decoder. Có chấp nhận được không?

---

## Proposed Changes

### Component: `FFmpegDecoder`

#### [MODIFY] [FFmpegDecoder.h](file:///d:/PROJECT/MyProjects/MediaSwitcher/src/engine/decoder/FFmpegDecoder.h)
- Thêm `maxVideoQueueSize` và `maxAudioQueueSize` có thể cấu hình
- Thêm method `setQueueLimits(size_t video, size_t audio)`

#### [MODIFY] [FFmpegDecoder.cpp](file:///d:/PROJECT/MyProjects/MediaSwitcher/src/engine/decoder/FFmpegDecoder.cpp)
- Giảm video queue limit từ `100` → `20`
- Thêm audio queue limit `200`
- Trong `readPackets()`: kiểm tra cả audio queue

---

### Component: `FramePool`

#### [MODIFY] [FramePool.h](file:///d:/PROJECT/MyProjects/MediaSwitcher/src/engine/frame/FramePool.h)
- Thêm `m_maxSize` (default = 3)

#### [MODIFY] [FramePool.cpp](file:///d:/PROJECT/MyProjects/MediaSwitcher/src/engine/frame/FramePool.cpp)
- `release()`: nếu pool đã đạt max size → drop frame (không thêm vào pool, destructor tự giải phóng)

---

### Component: `FileSource` (thay đổi lớn nhất)

#### [MODIFY] [FileSource.h](file:///d:/PROJECT/MyProjects/MediaSwitcher/src/engine/input/FileSource.h)
- Thêm enum `DecodeMode { Active, Idle }`
- Thêm `void setDecodeMode(DecodeMode mode)`
- Worker thread sẽ sleep dài hơn khi `Idle`

#### [MODIFY] [FileSource.cpp](file:///d:/PROJECT/MyProjects/MediaSwitcher/src/engine/input/FileSource.cpp)
- Khi `Idle`: sleep 500ms giữa các frame decode thay vì frame-paced decode liên tục
- Khi `Idle`: không decode audio, không buffer packets nhiều
- Khi `Idle`: sau khi vào idle, flush/clear packet queues để giải phóng RAM

---

### Component: `InputManager`

#### [MODIFY] [InputManager.cpp](file:///d:/PROJECT/MyProjects/MediaSwitcher/src/engine/input/InputManager.cpp)
- `setPreviewSlot()` / `setProgramSlot()`: cập nhật `DecodeMode` của tất cả sources:
  - Source mới là PVW/PGM → `Active`
  - Source cũ (không còn là PVW/PGM) → `Idle`

---

## Ước tính tiết kiệm RAM

| Tối ưu | Video 1080p (1 source) | Video 4K (1 source) |
|---|---|---|
| Giảm video queue 100→20 | ~10MB → ~2MB | ~50MB → ~10MB |
| Audio queue limit | ~5MB → ~1MB | ~5MB → ~1MB |
| Idle source (1fps decode) | ~80% giảm decode RAM | ~80% giảm |
| FramePool limit | ~15MB → ~3MB | ~60MB → ~12MB |
| **Tổng tiết kiệm/source** | **~20-25 MB** | **~100+ MB** |

Với 6 source (phổ biến): từ **300-600MB** xuống còn **~60-80MB**.

---

## Verification Plan

### Manual Verification
1. Build và chạy app
2. Load 4-6 file source khác nhau
3. Quan sát RAM trong Task Manager trước/sau
4. Switch PVW/PGM giữa các source – kiểm tra playback vẫn mượt
5. Kiểm tra không có frame drop hoặc lag khi switch từ idle → active
