# MediaSwitcher – Resource & Playback Architecture
## Implementation Specification v2

**Status:** Ready for implementation  
**Target:** Windows Desktop  
**Core:** C++ / Qt / FFmpeg / DirectX / XAudio2  
**Product type:** Minimal Media Switcher

---

# 1. Mục tiêu

MediaSwitcher cần hoạt động giống tư duy của một media switcher chuyên nghiệp:

```text
INPUT SOURCES
     ↓
SOURCE BROWSER
     ↓
THUMBNAIL / PREVIEW
     ↓
PROGRAM
     ↓
OUTPUT SCREEN
```

Ứng dụng phải hỗ trợ nhiều source nhưng không được giữ toàn bộ decoder/video/audio pipeline của tất cả source trong RAM.

## Mục tiêu tài nguyên

Ví dụ:

```text
100 sources
```

Không được dẫn đến:

```text
100 FFmpeg decoders
100 decode threads
100 audio queues
100 video queues
100 active playback pipelines
```

Thay vào đó:

```text
100 SourceInfo
100 thumbnails
+
1 Preview playback
+
1 Program playback
+
tối đa 1 Preload playback
```

---

# 2. Nguyên tắc kiến trúc quan trọng nhất

## SOURCE != PLAYBACK INSTANCE

Đây là nguyên tắc bắt buộc.

### Source

Là thông tin về một input:

```text
SourceInfo
├── id
├── name
├── path / URL
├── type
├── duration
├── width
├── height
├── fps
├── codec
├── thumbnail
└── state
```

Source không được tự động giữ:

```text
FFmpegDecoder
worker thread
packet queue
frame queue
audio buffer
XAudio2 voice
```

---

# 3. Playback Instance

Playback Instance là tài nguyên runtime dùng khi source thực sự được phát.

```text
PlaybackInstance
├── FFmpegDecoder
├── demuxer
├── video decoder
├── audio decoder
├── packet queues
├── frame queues
├── audio buffer
├── playback clock
└── output connection
```

Playback Instance chỉ tồn tại khi cần.

---

# 4. Source Lifecycle

Source có các state:

```text
IDLE
  ↓
PRELOADING
  ↓
READY
  ↓
PLAYING
  ↓
IDLE
```

Chi tiết:

## IDLE

Source chỉ giữ:

```text
metadata
thumbnail
```

Không decoder.

Không worker thread.

Không audio/video queue.

---

## PRELOADING

Khi user hover/click source hoặc hệ thống dự đoán source sắp được sử dụng:

```text
IDLE
 ↓
PRELOADING
```

Tạo playback instance tạm thời.

Mục tiêu:

```text
decoder ready
first frame decoded
audio initialized
```

---

## READY

Decoder đã sẵn sàng.

Preview có thể xuất hiện gần như ngay lập tức.

---

## PLAYING

Source đang là:

```text
PREVIEW
```

hoặc:

```text
PROGRAM
```

---

## EVICTION

Khi source không còn cần playback:

```text
PLAYING
 ↓
IDLE
```

Giải phóng:

```text
decoder
packet queue
frame queue
audio buffer
worker thread
XAudio2 resources
```

Nhưng giữ:

```text
SourceInfo
thumbnail
```

---

# 5. Kiến trúc tổng thể

```text
                         InputManager
                              │
                              ▼
                       SourceRegistry
                              │
             ┌────────────────┼────────────────┐
             ▼                ▼                ▼
         Source A          Source B         Source C
         metadata          metadata         metadata
         thumbnail         thumbnail        thumbnail
             │                │                │
             └────────────────┼────────────────┘
                              │
                              ▼
                      ResourceManager
                              │
                    ┌─────────┼─────────┐
                    ▼         ▼         ▼
                 PRELOAD    PREVIEW   PROGRAM
                    │         │         │
                    └─────────┼─────────┘
                              ▼
                       PlaybackManager
                              │
                        FFmpeg Decoder
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
                 VIDEO                AUDIO
                    │                   │
                 DirectX              XAudio2
                    │                   │
                    └─────────┬─────────┘
                              ▼
                           OUTPUT
```

---

# 6. Resource Budget

Không được để resource tăng vô hạn.

## Default budget

```text
MAX_PREVIEW_PLAYBACK = 1
MAX_PROGRAM_PLAYBACK = 1
MAX_PRELOAD_PLAYBACK = 1
```

Tổng:

```text
MAX_ACTIVE_PLAYBACK = 3
```

Nếu Preview và Program cùng source:

```text
có thể share playback pipeline
```

---

# 7. Packet Queue

Không được sử dụng queue không giới hạn.

Đặc biệt KHÔNG sử dụng:

```text
Audio queue is allowed to grow freely
```

Đây là lỗi kiến trúc.

## Audio Queue

Giới hạn theo:

```text
max duration
max bytes
max packets
```

Ví dụ:

```text
MAX_AUDIO_BUFFER_DURATION = 500ms
MAX_AUDIO_BUFFER_BYTES = configurable
```

## Video Queue

Tương tự:

```text
MAX_VIDEO_BUFFER_DURATION = 500ms
MAX_VIDEO_BUFFER_BYTES = configurable
MAX_VIDEO_PACKETS = configurable
```

Khi đạt giới hạn:

```text
STOP READING / DEMUX
```

Không tiếp tục đọc file để tạo thêm queue.

---

# 8. Frame Queue

Frame queue phải bounded.

Không được giữ unlimited full-resolution frames.

Ví dụ:

```text
Preview:
3–5 frames

Program:
3–5 frames
```

Có thể cấu hình.

---

# 9. Frame Memory

Không được đánh giá RAM dựa trên kích thước file.

Ví dụ:

```text
1920 × 1080 × RGBA
≈ 8.3 MB / frame
```

4K:

```text
3840 × 2160 × RGBA
≈ 33 MB / frame
```

Nếu giữ 10 frame 4K:

```text
≈ 330 MB
```

Do đó mọi full-resolution frame phải nằm trong budget.

---

# 10. FramePool

FramePool chỉ là cơ chế reuse.

FramePool KHÔNG được xem là memory limit.

Nếu:

```text
shared_ptr<Frame>
```

vẫn còn reference thì frame chưa thể quay lại pool.

Do đó cần:

```text
MAX_IN_FLIGHT_FRAMES
```

và kiểm soát ownership.

---

# 11. Thumbnail Architecture

Thumbnail không được dùng playback decoder lâu dài.

## Image

```text
Image file
 ↓
QImageReader
 ↓
scaled thumbnail
 ↓
cache
 ↓
release source image
```

Target:

```text
320 × 180
```

hoặc:

```text
480 × 270
```

Không giữ full-resolution image nếu không cần.

---

## Video

```text
Video file
 ↓
temporary FFmpeg decoder
 ↓
seek/decode frame
 ↓
resize thumbnail
 ↓
cache
 ↓
destroy temporary decoder
```

Chỉ giữ:

```text
thumbnail
```

Không giữ decoder.

---

# 12. Thumbnail Generation phải Async

Không được:

```text
Add Source
 ↓
decode thumbnail
 ↓
UI wait
 ↓
Add next Source
```

Phải:

```text
Add Source
 ↓
create SourceInfo
 ↓
show placeholder
 ↓
background thumbnail worker
 ↓
thumbnail ready
 ↓
update UI
```

UI không được block.

---

# 13. Add Source

`InputManager::addFileSlot()` không được tạo persistent decoder.

Nó chỉ:

```text
validate source
 ↓
read metadata
 ↓
create SourceInfo
 ↓
register source
 ↓
request thumbnail
 ↓
return immediately
```

Không:

```text
FileSource::open()
```

nếu source chưa cần playback.

---

# 14. Playback Manager

Tạo một component mới hoặc refactor tương đương:

```text
PlaybackManager
```

Responsibilities:

```text
createPreview(source)
createProgram(source)
preload(source)
release(source)
switchPreview(source)
cutToProgram()
```

---

# 15. Preview

Khi user click source:

```text
Source
 ↓
PlaybackManager
 ↓
ResourceManager
 ↓
reuse existing preload nếu có
 ↓
otherwise create playback
 ↓
decode first frame
 ↓
Preview
```

Preview phải xuất hiện nhanh.

Không cần load lại toàn bộ source nếu playback instance đã preload.

---

# 16. Program

Program là playback instance thực sự xuất ra màn hình.

```text
Program
 ↓
Decoder
 ↓
Video Frame
 ↓
DirectX
 ↓
Output Monitor
```

Audio:

```text
Audio
 ↓
XAudio2
 ↓
Speaker / Output device
```

---

# 17. Preview + Program cùng Source

Nếu:

```text
Preview = Source A
Program = Source A
```

không tạo hai decoder nếu không cần thiết.

Có thể:

```text
Decoder A
   │
   ├── Preview
   └── Program
```

Nếu implementation yêu cầu hai playback state độc lập thì cho phép hai instance, nhưng phải có resource budget.

---

# 18. Source Switching

Ví dụ:

```text
Program = A
Preview = B
```

User:

```text
CUT
```

Thực hiện:

```text
B
 ↓
Program
```

Sau CUT:

```text
old Program A
 ↓
release / idle
```

Không destroy source metadata.

---

# 19. Preload Strategy

Không preload tất cả source.

Chỉ preload:

```text
current Preview
next likely source
```

Maximum:

```text
1 preload instance
```

Nếu user đổi source liên tục:

```text
A → B → C → D
```

ResourceManager phải evict source cũ quando cần.

---

# 20. ResourceManager

ResourceManager chịu trách nhiệm:

```text
memory budget
decoder count
thread count
queue limits
preload
eviction
```

Ví dụ:

```text
ResourceManager
│
├── Preview instance
├── Program instance
├── Preload instance
│
├── memory usage
├── decoder count
└── thread count
```

---

# 21. Eviction Policy

Khi resource vượt budget:

```text
1. Release unused preload
2. Release inactive preview
3. Release old playback
4. Keep Program
5. Keep current Preview
```

Program có priority cao nhất.

```text
PROGRAM
  ↑
PREVIEW
  ↑
PRELOAD
  ↑
IDLE
```

Không bao giờ evict Program để giải phóng memory cho preload.

---

# 22. Audio – KHÔNG REFACTOR SANG QT Ở PHASE NÀY

Giữ:

```text
FFmpeg Audio Decoder
 ↓
Resampler
 ↓
Audio Buffer
 ↓
XAudio2
```

Chưa chuyển sang:

```text
QMediaPlayer
```

hoặc:

```text
QAudioSink
```

trong phase Resource Management.

Lý do:

Audio loop bug và resource management là hai vấn đề khác nhau.

Phải giải quyết lifecycle resource trước.

---

# 23. Audio Queue

Audio queue bắt buộc bounded.

Không:

```text
unlimited audio packet queue
```

Phải:

```text
decode
 ↓
bounded queue
 ↓
XAudio2
```

Nếu queue đầy:

```text
pause demux
```

Khi queue giảm:

```text
resume demux
```

---

# 24. Audio Loop

Sau khi Resource Architecture hoàn thành mới xử lý:

```text
EOF
 ↓
drain audio packets
 ↓
drain codec
 ↓
drain resampler
 ↓
finish pending PCM
 ↓
reset audio playback
 ↓
seek 0
 ↓
flush codec
 ↓
restart
```

Không được:

```text
EOF
 ↓
clear audio buffer
 ↓
seek
```

---

# 25. Không Loop sớm bằng Video Position

Không dùng logic kiểu:

```text
position >= duration - 50ms
```

để tự động loop nếu audio chưa hoàn thành.

EOF phải dựa trên media state thực tế.

Video EOF và Audio EOF phải được quản lý đúng lifecycle.

---

# 26. Thread Architecture

Không tạo worker thread cho mỗi source idle.

Sai:

```text
100 sources
 ↓
100 workers
```

Đúng:

```text
Source Registry
 ↓
shared worker / limited worker pool

Playback:
Preview worker
Program worker
Preload worker
```

Thread count phải có giới hạn.

---

# 27. Resource Metrics

Phải có debug panel:

```text
Sources: 100

Active Decoders: 2
Preload Decoders: 0

Video Queued: 4 frames
Audio Buffered: 180 ms

Threads: 6

CPU: 12%
RAM: 420 MB
GPU: 280 MB
```

Cho phép kiểm tra architecture bằng số liệu thực tế.

---

# 28. Acceptance Test

## Test 1 – 10 sources

```text
10 sources
```

Kiểm tra:

- RAM
- thread count
- decoder count
- add time

---

## Test 2 – 50 sources

```text
50 sources
```

Expected:

```text
No 50 active decoders.
No 50 worker threads.
No unbounded queues.
```

---

## Test 3 – 100 sources

```text
100 sources
```

Expected:

```text
100 thumbnails + metadata
+
limited active playback
```

RAM phải tăng gần tuyến tính theo thumbnail/metadata, không tăng theo decoder/video/audio pipeline.

---

# 29. Functional Acceptance

Resource optimization KHÔNG được làm mất:

- File input
- Image input
- RTSP
- NDI
- Thumbnail
- Preview
- Program
- CUT
- Play
- Pause
- Seek
- Loop
- Audio
- Video
- Fullscreen output

Đặc biệt:

```text
100 sources
 ↓
click source
 ↓
Preview vẫn hoạt động
 ↓
CUT
 ↓
Program vẫn hoạt động
```

---

# 30. Performance Acceptance

Mục tiêu:

```text
Add source:
UI không block.

Thumbnail:
background.

Idle source:
không có decoder.

Idle source:
không có decode thread.

Idle source:
không có audio queue.

Idle source:
không có video queue.

Active playback:
resource bounded.
```

---

# 31. Không được Refactor Big Bang

Agent phải làm từng phase.

## Phase 1

Audit hiện trạng.

Không sửa code.

Report:

```text
objects per source
threads per source
decoders per source
queues per source
memory per source
```

---

## Phase 2

Tách:

```text
SourceInfo
```

khỏi:

```text
PlaybackInstance
```

---

## Phase 3

Refactor `InputManager`.

Add Source không tạo persistent decoder.

---

## Phase 4

Async thumbnail.

---

## Phase 5

Tạo `PlaybackManager`.

---

## Phase 6

Tạo `ResourceManager`.

---

## Phase 7

Bounded queues.

---

## Phase 8

Frame memory limits.

---

## Phase 9

Preload.

---

## Phase 10

Stress test:

```text
10
20
50
100
```

---

## Phase 11

Quay lại Audio EOF/Loop.

---

# 32. Không được tự ý thay công nghệ

Không tự ý thay:

```text
FFmpeg
DirectX
XAudio2
```

bằng framework khác.

Không chuyển sang Qt Multimedia trong phase này.

Không viết lại toàn bộ decoder.

Không rewrite UI.

Không thay đổi Input/Preview/Program behavior nếu không cần thiết.

---

# 33. Definition of Done

Phase Resource Architecture chỉ hoàn thành khi:

```text
100 sources
        ↓
metadata + thumbnails
        ↓
RAM controlled
        ↓
no decoder explosion
        ↓
no thread explosion
        ↓
no unbounded queues
        ↓
click source
        ↓
fast Preview
        ↓
CUT
        ↓
stable Program
```

Quan trọng nhất:

> **Không tối ưu bằng cách làm giảm chức năng.**

Mục tiêu là:

```text
NHIỀU SOURCE
     +
ÍT RESOURCE ACTIVE
     +
PREVIEW NHANH
     +
PROGRAM ỔN ĐỊNH
```

Đây là kiến trúc cốt lõi của MediaSwitcher.