# Project Structure

```text
MediaSwitcher

src/

    app/

    ui/

    engine/

        input/

        decoder/

        renderer/

        output/

        frame/

        gpu/

    plugins/

        file/

        rtsp/

        ndi/

        image/

    common/

        logger/

        config/

        thread/

        utils/

assets/

docs/

tests/

third_party/

    ffmpeg/

    ndi/

    fmt/

    spdlog/

workspace/

logs/

build/
```

## Third Party Libraries

- Qt 6
- FFmpeg
- NDI SDK
- spdlog
- fmt
- DirectX 11 SDK

## Build System

- CMake
- Ninja (optional)
- MSVC 2022

## Deliverable

```text
MediaSwitcher/

    MediaSwitcher.exe

    assets/

    ffmpeg/

    ndi/

    plugins/

    workspace/

    logs/

    config.json
```

Portable, không cần cài đặt.