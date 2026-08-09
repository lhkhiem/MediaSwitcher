# Find Packages
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# DirectX 11 Libraries
set(DIRECTX_LIBRARIES d3d11.lib dxgi.lib d3dcompiler.lib ole32.lib)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${FFMPEG_INCLUDE_DIR})
link_directories(${FFMPEG_LIB_DIR})

# Source files
set(SOURCES
    main.cpp
    resource.rc
    common/logger/Logger.cpp
    common/logger/Logger.h
    common/config/Config.cpp
    common/config/Config.h
    common/config/AppInfo.h
    app/WorkspaceManager.cpp
    app/WorkspaceManager.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/AboutDialog.cpp
    ui/AboutDialog.h
    ui/FullscreenLEDWindow.cpp
    ui/FullscreenLEDWindow.h
    ui/InputSlotWidget.cpp
    ui/InputSlotWidget.h
    ui/GlobalPlaylistDialog.cpp
    ui/GlobalPlaylistDialog.h
    ui/AudioMeterWidget.cpp
    ui/AudioMeterWidget.h
    engine/renderer/DirectXWindow.cpp
    engine/renderer/DirectXWindow.h
    engine/renderer/Renderer.cpp
    engine/renderer/Renderer.h
    engine/renderer/Texture.cpp
    engine/renderer/Texture.h
    engine/frame/Frame.h
    engine/frame/FramePool.cpp
    engine/frame/FramePool.h
    engine/input/IMediaSource.h
    engine/input/InputSlot.h
    engine/input/InputManager.cpp
    engine/input/InputManager.h
    engine/input/ColorBarsSource.cpp
    engine/input/ColorBarsSource.h
    engine/input/FileSource.cpp
    engine/input/FileSource.h
    engine/input/GlobalPlaylistController.h
    engine/decoder/FFmpegDecoder.cpp
    engine/decoder/FFmpegDecoder.h
    engine/audio/AudioEngine.cpp
    engine/audio/AudioEngine.h
)

# MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Executable
add_executable(MediaSwitcher WIN32 ${SOURCES})

# FFmpeg Libraries
set(FFMPEG_LIBRARIES
    avcodec.lib
    avformat.lib
    avutil.lib
    swscale.lib
    swresample.lib
)

# Link libraries
target_link_libraries(MediaSwitcher PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
    fmt::fmt
    ${DIRECTX_LIBRARIES}
    ${FFMPEG_LIBRARIES}
)

# Copy FFmpeg DLLs to output directory post-build
add_custom_command(TARGET MediaSwitcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${FFMPEG_BIN_DIR}"
    $<TARGET_FILE_DIR:MediaSwitcher>
)

# Deploy Qt6 DLLs to target executable post-build
get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
add_custom_command(TARGET MediaSwitcher POST_BUILD
    COMMAND "${QT_BIN_DIR}/windeployqt.exe" --no-compiler-runtime --no-system-d3d-compiler "$<TARGET_FILE:MediaSwitcher>"
)

# Compile options
if(MSVC)
    target_compile_options(MediaSwitcher PRIVATE /W4)
endif()
