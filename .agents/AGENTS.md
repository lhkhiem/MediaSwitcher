# Workspace Rules for MediaSwitcher

## Qt6 DLL Deployment Rule
- Whenever configuring or building CMake targets using Qt (Qt6), ALWAYS ensure `windeployqt` is included as a `POST_BUILD` custom command in `CMakeLists.txt` or executed immediately after building.
- Target `windeployqt` specifically at the target executable file (`$<TARGET_FILE:MediaSwitcher>`) rather than the folder directory to prevent dependency scanning conflicts with non-Qt binaries (such as FFmpeg binaries).
- Always verify that `Qt6Widgets.dll`, `Qt6Core.dll`, `Qt6Gui.dll`, and `platforms/qwindows.dll` are present alongside the compiled `.exe` before attempting execution.
