# 08_AI_AGENT_RULES.md

# AI Development Rules

## Project Philosophy

MediaSwitcher is a professional Windows media switcher.

The primary goals are:

- Stability
- Performance
- Simplicity
- Low latency
- Portable

Every implementation decision must prioritize these goals.

---

# General Principles

## Stability First

Never sacrifice stability for new features.

A stable application with fewer features is preferred over an unstable application with many features.

---

## Performance First

Every frame matters.

Avoid unnecessary:

- Memory allocation
- Copy
- GPU upload
- Thread synchronization

---

## Simplicity

Prefer simple solutions.

Avoid over-engineering.

Avoid unnecessary abstractions.

---

# Architecture Rules

UI must never decode media.

UI must never process video frames.

UI only sends commands.

---

Media Engine owns all media processing.

Media Engine owns:

- Decoder
- Renderer
- GPU
- Audio
- Output

---

Every Input has exactly ONE Decoder.

Never create multiple decoders for the same source.

Correct:

Source

↓

Decoder

↓

Frame

↓

Thumbnail

Preview

Program

Wrong:

Source

↓

Decoder A

↓

Thumbnail

Source

↓

Decoder B

↓

Preview

---

All modules communicate through interfaces.

Never couple modules directly.

---

# Thread Rules

UI Thread

Allowed:

- Draw UI
- Receive Input
- Send Commands

Forbidden:

- Decode
- GPU Upload
- FFmpeg API
- NDI Receive
- Heavy Processing

---

Worker Thread

Responsible for:

- Decode
- Read File
- RTSP Receive
- NDI Receive

---

Render Thread

Responsible for:

- GPU Upload
- Texture Update
- Preview
- Program
- Thumbnail

---

Never block UI Thread.

---

# Memory Rules

Avoid raw pointers.

Prefer:

std::unique_ptr

Use std::shared_ptr only when ownership is shared.

Never allocate memory every frame.

Allocate once.

Reuse.

---

Frame buffers must be recycled.

Do not continuously create Frame objects.

---

# Renderer Rules

Renderer only renders.

Renderer must never:

- Open files
- Decode media
- Read RTSP
- Receive NDI

Renderer only accepts:

Frame

↓

Texture

↓

Render

---

# Decoder Rules

Decoder only decodes.

Decoder must never know:

- Preview
- Program
- UI

Decoder returns only Frame.

---

# Frame Rules

Frame is immutable after creation.

Consumers must never modify Frame data.

Frame metadata:

- Width
- Height
- Pixel Format
- Timestamp

---

# Plugin Rules

Each media source is a plugin.

Examples:

FileSource

RTSPSource

NDISource

ImageSource

Future:

BrowserSource

PDFSource

PowerPointSource

Each plugin implements IMediaSource.

---

# UI Rules

UI must remain responsive.

Target:

60 FPS UI

Even if decoding multiple streams.

---

Never place FFmpeg logic inside UI.

Never place DirectX logic inside Widgets.

---

# Logging Rules

Use spdlog.

Levels:

INFO

WARN

ERROR

DEBUG

Never use printf().

Never ignore exceptions.

Every failure must be logged.

---

# Error Handling

Application must never crash because:

- Missing file
- Broken RTSP
- Lost NDI
- Unsupported codec

Display user-friendly error messages.

Attempt automatic recovery whenever possible.

---

# Code Style

Maximum file size:

500 lines

Maximum function size:

50 lines

Maximum class responsibility:

One responsibility only.

---

# Build Rules

Compiler:

MSVC 2022

Language:

C++20

Build:

CMake

Warnings:

Treat warnings as errors.

---

# Third Party Libraries

Approved:

Qt6

FFmpeg

NDI SDK

spdlog

fmt

DirectX11

Forbidden:

Boost (unless absolutely necessary)

Large GUI frameworks other than Qt

Electron

Chromium

---

# Performance Targets

Startup

< 3 seconds

Preview latency

< 100 ms

Cut transition

< 1 frame

CPU

As low as possible

Memory

No memory leak

Continuous operation

12+ hours

---

# AI Coding Rules

Before generating code, always check:

- Does this violate project architecture?
- Does this introduce unnecessary coupling?
- Can this increase latency?
- Can this allocate memory every frame?
- Can this block the UI thread?

If the answer is YES,

Do not implement.

Refactor first.

---

# Future Compatibility

Every module must be replaceable.

Example:

Today:

FFmpeg Decoder

Tomorrow:

Hardware Decoder

The Renderer must not change.

The UI must not change.

Only the Decoder implementation changes.

---

# Golden Rule

One Decode

↓

One Frame

↓

Multiple Render Targets

Never decode the same media twice.

This rule must never be violated.