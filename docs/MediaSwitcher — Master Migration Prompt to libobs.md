# MediaSwitcher — Master Migration Prompt to libobs

Repository: `MediaSwitcher`

Tech stack hiện tại:

- C++20
- Qt6 Widgets
- FFmpeg
- XAudio2
- CMake
- Windows
- Repository: `https://github.com/lhkhiem/MediaSwitcher`

## Quyết định kiến trúc

Từ thời điểm này, custom FFmpeg/XAudio2 playback engine hiện tại được xem là **Legacy Playback Backend**.

Không tiếp tục đầu tư refactor sâu vào custom audio/video synchronization engine.

Mục tiêu mới là:

> Sử dụng OBS/libobs làm media engine nền tảng và phát triển MediaSwitcher thành một frontend/operator workflow riêng.

Không fork UI của OBS Studio.

Không biến MediaSwitcher thành OBS clone.

Ta kế thừa media infrastructure của OBS nhưng giữ workflow riêng của MediaSwitcher.

---

# 1. RESPONSIBILITY BOUNDARY

## libobs chịu trách nhiệm

- media decode
- audio playback
- audio/video synchronization
- buffering
- timestamp handling
- media source lifecycle
- rendering infrastructure
- GPU graphics infrastructure
- future capture/network/plugin inputs where appropriate

## MediaSwitcher chịu trách nhiệm

- Source Grid
- MediaAsset management
- Preview / PVW
- Program / PGM
- Preload
- TAKE
- QUICK PLAY
- CUT
- FADE
- Playlist
- Next / Previous
- Operator workflow
- LED presentation workflow
- role orchestration

Không đưa low-level FFmpeg/XAudio2 logic vào PlaybackManager sau migration.

---

# 2. CURRENT IMPORTANT WORKFLOW SEMANTICS

Các semantics hiện tại phải được bảo toàn.

## Playback roles

```text
Preview
Program
Preload
```

Giữ:

```text
MAX_TOTAL_ACTIVE_PLAYBACKS = 3
```

Không tạo playback instance vô hạn.

---

# 3. PREVIEW

Preview và Program phải độc lập.

Thông thường khi operator chọn một source:

```text
PVW = selected source
PVW = PAUSED
```

PVW phải có position riêng.

---

# 4. PROGRAM

Program thông thường:

```text
PGM = PLAYING
```

Program phải hoạt động độc lập với Preview.

---

# 5. TAKE SEMANTICS

TAKE là:

```text
PVW -> PGM promotion
```

TAKE KHÔNG phải swap.

Sau TAKE:

```text
PGM = source vừa TAKE
PGM = PLAYING

PVW = PAUSED
```

Không được thay đổi semantics này khi tích hợp OBS.

---

# 6. QUICK PLAY / CUT / FADE

Các operation này đang dùng role-based switching/swapping semantics.

Sau operation phải giữ invariant:

```text
PGM = PLAYING
PVW = PAUSED
```

Không để Preview tự play ngoài ý muốn.

---

# 7. SAME MEDIA ASSET — INDEPENDENT PLAYBACK

Đây là yêu cầu quan trọng.

Một media file có thể đồng thời tồn tại ở PVW và PGM với state khác nhau.

Ví dụ:

```text
Wedding.mp4

PVW:
position = 90s
PAUSED

PGM:
position = 30s
PLAYING
```

Do đó:

```text
Media file != single runtime playback instance
```

Không được mặc định:

```text
1 file = 1 obs_source_t
```

Cần phân biệt:

```text
MediaAsset
```

và:

```text
PlaybackInstance
```

Concept:

```text
MediaAsset
   |
   +-- PreviewPlaybackInstance
   |       |
   |       +-- obs_source_t
   |
   +-- ProgramPlaybackInstance
   |       |
   |       +-- obs_source_t
   |
   +-- PreloadPlaybackInstance
           |
           +-- obs_source_t
```

Implementation thực tế phải dựa vào repository hiện tại, không blindly copy diagram này.

---

# 8. TARGET ARCHITECTURE

Hướng kiến trúc mong muốn:

```text
MediaSwitcher Qt UI
        |
        v
PlaybackManager
        |
        v
Playback Backend Boundary
      /        \
Legacy          OBS
Backend          Backend
                  |
                  v
                libobs
```

OBS backend có thể gồm:

```text
ObsContext
ObsModuleLoader
ObsPlaybackBackend
ObsMediaSource
ObsView
ObsDisplay
```

Chỉ tạo abstraction thực sự cần thiết.

Không overengineer.

---

# 9. LEGACY ENGINE

Hiện tại project có các thành phần custom như:

```text
FFmpegDecoder
AudioEngine
FileSource
Renderer
```

Không xóa chúng ngay.

Không rewrite chúng trong Phase đầu.

Chúng phải tồn tại như Legacy Backend cho đến khi OBS backend đạt feature parity.

Sau khi OBS backend đã pass runtime/stress tests mới xóa legacy engine.

---

# 10. OBS INITIALIZATION

Audit và thiết kế lifecycle đúng cho:

```text
obs_startup()
obs_reset_video()
obs_reset_audio()
obs_load_all_modules()
obs_post_load_modules()
obs_shutdown()
```

Một class phải có ownership rõ ràng cho lifecycle này.

Không để nhiều object tự gọi startup/shutdown.

Shutdown phải deterministic.

Tất cả `obs_source_t`, view, display và OBS references phải release đúng trước khi `obs_shutdown()`.

---

# 11. OBS MODULES

Không tích hợp toàn bộ OBS Studio ngay.

Chỉ sử dụng những module cần cho media playback ban đầu.

Audit module cần thiết cho:

```text
media file playback
FFmpeg/media source
image source nếu cần
audio
graphics
```

Sau này mới mở rộng:

```text
camera
capture card
DeckLink
NDI
RTSP/SRT
browser source
recording
streaming
```

---

# 12. OBS VIEW / DISPLAY

Không sử dụng OBS Studio Mode làm workflow chính.

MediaSwitcher phải có Preview và Program riêng.

Investigate:

```text
obs_view_t
obs_display_t
```

để tạo:

```text
PreviewView
ProgramView
```

và render vào Qt widgets hiện tại.

Concept:

```text
             MediaSwitcher
                   |
        +----------+----------+
        |                     |
        v                     v
   PreviewView           ProgramView
        |                     |
    obs_view_t             obs_view_t
        |                     |
    Qt Widget             Qt Widget
```

Tận dụng OBS rendering infrastructure.

Không tự viết thêm renderer nếu OBS có thể đảm nhiệm.

---

# 13. AUDIO STRATEGY

OBS sẽ trở thành audio engine chính.

Sau migration không tiếp tục dùng custom XAudio2 AudioEngine cho OBS playback.

Phải audit và thiết kế rõ:

```text
Program audio
Preview audio
```

Program audio phải output bình thường.

Preview audio mặc định nên không phát ra main output nếu workflow hiện tại yêu cầu Preview silent.

Không implement audio routing tùy tiện trước khi hiểu OBS source monitoring/muting model.

---

# 14. PLAYBACK BACKEND INTERFACE

Investigate abstraction tối thiểu cần thiết.

Ví dụ concept:

```cpp
class IPlaybackBackend
{
public:
    virtual ~IPlaybackBackend() = default;

    virtual bool open(...) = 0;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;

    virtual bool seek(...) = 0;

    virtual PlaybackState state() const = 0;

    virtual double position() const = 0;
    virtual double duration() const = 0;
};
```

Không blindly implement interface trên.

Phải audit repository để thiết kế interface phù hợp với current state machine.

Có thể sử dụng adapter:

```text
LegacyPlaybackBackend
ObsPlaybackBackend
```

---

# 15. PLAYBACK MANAGER

PlaybackManager sau migration chỉ nên quản lý:

```text
PVW
PGM
Preload

role assignment
lifecycle
operator commands
```

Không nên biết:

```text
AVPacket
AVFrame
PCM
XAudio2 buffers
OBS internals
```

Giữ dependency boundary rõ.

---

# 16. BUILD SYSTEM

Audit `CMakeLists.txt` hiện tại.

Xác định:

- Qt6 configuration
- FFmpeg FetchContent hiện tại
- Windows build structure
- libobs dependency strategy

Đề xuất cách tích hợp libobs reproducible.

Không phụ thuộc vào kiểu:

```text
C:\Program Files\obs-studio\...
```

hardcoded.

Investigate các phương án:

```text
OBS source dependency
git submodule
FetchContent
prebuilt SDK/dependency package
ExternalProject
controlled local dependency
```

Chọn phương án phù hợp nhất với Windows + CMake + developer workflow của repo.

---

# 17. LICENSING

OBS/libobs sử dụng GPL.

Repository MediaSwitcher hiện có proprietary/all-rights-reserved language.

Không sửa license trong Phase đầu.

Nhưng phải ghi rõ đây là **release blocker** cần xử lý trước khi phát hành binary có link libobs.

Không được bỏ qua vấn đề này.

---

# 18. MIGRATION STRATEGY

Không rewrite một lần.

Thực hiện migration theo phase.

## Phase 0 — Architecture Audit

Không sửa code.

Đọc toàn bộ repository.

Audit ít nhất:

```text
PlaybackManager
FileSource
FFmpegDecoder
AudioEngine
Renderer
MainWindow
InputSlotWidget
playlist code
role/state abstractions
CMakeLists.txt
```

Xác định:

```text
ownership
threading
state model
role model
dependencies
renderer ownership
audio ownership
```

---

## Phase 1 — OBS Foundation

Mục tiêu:

```text
Legacy app vẫn hoạt động

+

libobs initialize thành công

+

OBS modules load được

+

ObsPlaybackBackend skeleton tồn tại
```

Chưa thay playback thật.

Không sửa TAKE/CUT/Playlist.

---

## Phase 2 — Single Media Prototype

Chỉ làm:

```text
1 media file
    |
    v
OBS media source
    |
    v
one Qt preview
```

Features:

```text
open
play
pause
seek
position
duration
audio
```

Không tích hợp toàn workflow.

---

## Phase 3 — OBS Rendering

Ổn định:

```text
obs_source_t
    |
obs_view_t
    |
obs_display_t
    |
Qt Widget
```

Test resize, fullscreen, shutdown.

---

## Phase 4 — Independent PVW / PGM

Tạo runtime playback instances độc lập.

Test:

```text
PVW source A @ 90s paused
PGM source A @ 30s playing
```

Phải hoạt động.

---

## Phase 5 — Playback Parity

Implement:

```text
play
pause
seek
stop
duration
position
loop
EOF
```

---

## Phase 6 — TAKE / QUICK PLAY / CUT

Reconnect operator semantics.

Verify:

```text
TAKE != swap
```

and:

```text
after operation:
PGM PLAYING
PVW PAUSED
```

---

## Phase 7 — FADE

Implement transition thông qua OBS graphics/composition infrastructure nếu phù hợp.

Không tự viết CPU transition nếu OBS có solution tốt hơn.

---

## Phase 8 — Playlist / Preload

Reconnect:

```text
Next
Previous
Loop
Auto Next
Preload
```

Giữ:

```text
MAX_TOTAL_ACTIVE_PLAYBACKS = 3
```

---

## Phase 9 — Stress Testing

Test:

```text
2–4 hour playback
100+ seeks
100+ TAKE
100+ Quick Play
100+ CUT
loop repeatedly
same-source PVW/PGM
playlist auto-next
pause/resume
```

Watch:

```text
RAM
CPU
GPU
audio continuity
video continuity
A/V sync
OBS references
crashes
deadlocks
```

---

## Phase 10 — Remove Legacy Engine

Chỉ khi OBS backend đạt feature parity.

Sau đó mới remove:

```text
custom FFmpeg decoder
custom AudioEngine
XAudio2 pipeline
custom renderer portions no longer needed
```

Không xóa trước.

---

# 19. CURRENT KNOWN LEGACY AUDIO PROBLEMS

Custom engine từng gặp:

```text
audio micro-stutter
seek audio residue
XAudio2 buffers surviving seek
audio producer potentially blocked by video
silence injection
head-of-line blocking
vector erase on audio buffer
audio PTS reset issues
lack of robust audio master clock
```

Không cần tiếp tục redesign sâu những vấn đề này nếu OBS backend sẽ thay thế engine.

Legacy engine chỉ cần đủ ổn để phục vụ migration fallback.

---

# 20. IMPORTANT RULES FOR CODEX

Không sửa code ngay khi bắt đầu.

Trước tiên hãy audit repository.

Không dựa trên assumptions trong prompt nếu source code chứng minh khác.

Source code hiện tại là source of truth.

Không phá working workflow chỉ để đạt architecture đẹp.

Không rename hàng loạt class vô ích.

Không refactor UI nếu không cần.

Không add features ngoài migration.

Không xóa legacy engine.

Không claim success vì compile.

Không claim OBS playback stable nếu chưa runtime test trên Windows PC.

Không commit trực tiếp vào `main`.

Work trên branch:

```text
feature/libobs-backend
```

---

# 21. FIRST REQUIRED OUTPUT

Trước khi code, hãy trả về report:

## A. Current architecture

Map:

```text
MainWindow
PlaybackManager
FileSource
FFmpegDecoder
AudioEngine
Renderer
Playlist
```

## B. Ownership graph

Ai sở hữu object nào.

## C. Thread model

Thread nào hiện đang tồn tại.

## D. Playback role implementation

Verify actual implementation của:

```text
Preview
Program
Preload
TAKE
Quick Play
CUT
FADE
```

## E. Current media data flow

Từ file đến display/audio.

## F. Proposed OBS architecture

Map từng class:

```text
KEEP
ADAPT
NEW
REPLACE LATER
REMOVE AFTER MIGRATION
```

## G. Playback backend boundary

Đề xuất interface tối thiểu.

## H. MediaAsset vs PlaybackInstance model

Đặc biệt xử lý same-source PVW/PGM.

## I. OBS lifecycle

Owner của:

```text
obs_startup
obs_reset_video
obs_reset_audio
module loading
obs_shutdown
```

## J. Preview / Program views

Map `obs_view_t` / `obs_display_t` vào Qt.

## K. Audio strategy

PVW/PGM routing.

## L. CMake/libobs integration

Đề xuất dependency strategy.

## M. Exact files

```text
FILES TO ADD

FILES TO MODIFY

FILES TO KEEP UNTOUCHED

FILES TO REMOVE LATER
```

## N. Regression risks

Liệt kê cụ thể.

## O. Phase 1 implementation plan

Phase 1 phải nhỏ, reversible và buildable.

---

# 22. AFTER AUDIT

Sau khi hoàn tất audit:

STOP.

Do not implement automatically.

Wait for approval before modifying source code.

The first implementation milestone will be:

```text
Existing MediaSwitcher still works

+

libobs starts successfully

+

required OBS modules can be loaded

+

ObsPlaybackBackend skeleton exists
```

Không làm hơn milestone này trong Phase 1.