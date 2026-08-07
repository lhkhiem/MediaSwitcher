# Module Specification

## InputManager

Responsibility

Quản lý tất cả Source.

API

- add()
- remove()
- open()
- close()

---

## DecoderManager

Responsibility

Decode Video.

API

- decode()
- seek()
- pause()
- play()

---

## FrameManager

Responsibility

Cache Frame.

API

- getLatestFrame()
- pushFrame()

---

## ThumbnailManager

Responsibility

Generate Thumbnail.

Không Decode.

Chỉ Resize.

---

## PreviewManager

Responsibility

Render Preview.

---

## ProgramManager

Responsibility

Render Output.

---

## OutputManager

Responsibility

Fullscreen.

Monitor Select.

Resolution.