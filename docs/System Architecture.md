# System Architecture

## Layer

```text
Qt UI

↓

Application Layer

↓

Media Engine

↓

Hardware
```

## Components

Application

- Command Dispatcher
- Workspace Manager
- Settings

Media Engine

- Input Manager
- Decoder Manager
- Frame Manager
- Renderer
- Output Manager

Hardware

- GPU
- Monitor
- Audio

## Pipeline

```text
Source

↓

Decoder

↓

Frame Queue

↓

GPU Texture

↓

Thumbnail

↓

Preview

↓

Program
```

## Design Rules

- UI không Decode
- Renderer không Decode
- Một Source chỉ có một Decoder
- Một Frame dùng cho nhiều Output
- Không Copy Memory nhiều lần
- Không Block UI Thread