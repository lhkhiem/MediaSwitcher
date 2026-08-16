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
    resources.qrc
    common/logger/Logger.cpp
    common/logger/Logger.h
    common/config/Config.cpp
    common/config/Config.h
    common/config/AppInfo.h
    common/config/CopyrightSettings.cpp
    common/config/CopyrightSettings.h
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
    engine/input/SourceInfo.h
    engine/input/SourceRegistry.h
    engine/input/SourceRegistry.cpp
    engine/input/ThumbnailGenerator.h
    engine/input/ThumbnailGenerator.cpp
    engine/input/ResourceManager.h
    engine/input/PlaybackManager.h
    engine/input/PlaybackManager.cpp
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
    engine/diagnostics/MediaDiagnostics.cpp
    engine/diagnostics/MediaDiagnostics.h
)

if (MEDIASWITCHER_ENABLE_OBS)
    list(APPEND SOURCES
        engine/obs/ObsContext.cpp
        engine/obs/ObsContext.h
        engine/obs/ObsPlaybackBackend.cpp
        engine/obs/ObsPlaybackBackend.h
        engine/obs/ObsPlaylist.cpp
        engine/obs/ObsPlaylist.h
        engine/obs/ObsSourceCatalog.cpp
        engine/obs/ObsSourceCatalog.h
        ui/ObsDualMediaTestWindow.cpp
        ui/ObsDualMediaTestWindow.h
        ui/ObsLiveThumbnailWidget.cpp
        ui/ObsLiveThumbnailWidget.h
        ui/ObsProgramOutputWindow.cpp
        ui/ObsProgramOutputWindow.h
        ui/ObsMediaTestWindow.cpp
        ui/ObsMediaTestWindow.h
    )
endif()

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

if (MEDIASWITCHER_ENABLE_OBS)
    # libobs Windows resolves data and plugin paths relative to bin/64bit.
    set(OBS_DEPLOY_ROOT "${CMAKE_BINARY_DIR}/obs-runtime-stage")
    set_target_properties(MediaSwitcher PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OBS_DEPLOY_ROOT}/bin/64bit"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OBS_DEPLOY_ROOT}/bin/64bit"
    )

    target_link_libraries(MediaSwitcher PRIVATE OBS::libobs)
    target_compile_definitions(MediaSwitcher PRIVATE MEDIASWITCHER_ENABLE_OBS=1)

    add_custom_command(TARGET MediaSwitcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OBS_RUNTIME_BIN_DIR}"
            "$<TARGET_FILE_DIR:MediaSwitcher>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OBS_RUNTIME_PLUGIN_BIN_DIR}"
            "$<TARGET_FILE_DIR:MediaSwitcher>/../../obs-plugins/64bit"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OBS_RUNTIME_PLUGIN_DATA_DIR}"
            "$<TARGET_FILE_DIR:MediaSwitcher>/../../data/obs-plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OBS_RUNTIME_CORE_DATA_DIR}"
            "$<TARGET_FILE_DIR:MediaSwitcher>/../../data/libobs"
    )
endif()
# Copy FFmpeg DLLs to output directory post-build
add_custom_command(TARGET MediaSwitcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${FFMPEG_BIN_DIR}"
    $<TARGET_FILE_DIR:MediaSwitcher>
)

# Copy app icon to output directory so QIcon can load it at runtime
add_custom_command(TARGET MediaSwitcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.ico"
    "$<TARGET_FILE_DIR:MediaSwitcher>/app_icon.ico"
)

# Deploy Qt6 DLLs to target executable post-build
get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
add_custom_command(TARGET MediaSwitcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${QT_BIN_DIR}\\;$ENV{PATH}" "${QT_BIN_DIR}/windeployqt.exe" --no-compiler-runtime --no-system-d3d-compiler "$<TARGET_FILE:MediaSwitcher>"
)

# Compile options
if(MSVC)
    target_compile_options(MediaSwitcher PRIVATE /W4)
endif()
