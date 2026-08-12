# Controlled OBS Runtime 32.1.2

MediaSwitcher Phase 1.5 uses only a controlled Windows x64 subset of the official `OBS-Studio-32.1.2-Windows-x64.zip` artifact. The official runtime ZIP SHA-256 is:

```text
8d97e4563bd8d22d03e63042aa7dccede1d555c9bd35ce8a9e5019b0d0201bf6
```

The matching source-tag archive SHA-256 is `b4a59410cddb46d0e31df1ee13b8ec66f30862d7e980c1a8c4e3b5d16fae6053`. Run `tools\PrepareObsRuntime.ps1` to create the runtime. The generated `runtime-manifest.json` pins the ABI version and architecture; CMake rejects roots without it.

## Layout

```text
obs-runtime/
  include/                 # libobs headers from source tag 32.1.2
  lib/obs.lib              # import library generated from matching obs.dll
  bin/64bit/               # libobs core, D3D11 backend and required DLL dependencies
  obs-plugins/64bit/       # obs-ffmpeg.dll only
  data/libobs/             # required libobs effect files
  data/obs-plugins/obs-ffmpeg/
  runtime-manifest.json
```

When OBS is enabled, the build deploys the executable to `obs-runtime-stage/bin/64bit` and keeps `data/` plus `obs-plugins/` two levels above it, matching libobs Windows path resolution.

The selected Phase 1.5 subset is `obs.dll`, `libobs-d3d11.dll`, OBS-bundled FFmpeg DLLs, including `avfilter-10.dll` and dependencies needed by `obs-ffmpeg.dll`. It deliberately excludes OBS Studio UI, browser, capture, encoder, streaming and hardware-specific plugins.

Do not mix files between OBS versions or builds. This runtime is for foundation validation only; it does not yet create media sources.