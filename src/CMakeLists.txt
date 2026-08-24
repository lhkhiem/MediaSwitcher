find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

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
    engine/diagnostics/MediaDiagnostics.cpp
    engine/diagnostics/MediaDiagnostics.h
    engine/obs/ObsContext.cpp
    engine/obs/ObsContext.h
    engine/obs/ObsPlaybackBackend.cpp
    engine/obs/ObsPlaybackBackend.h
    engine/obs/ObsPlaylist.cpp
    engine/obs/ObsPlaylist.h
    engine/obs/ObsSourceCatalog.cpp
    engine/obs/ObsSourceCatalog.h
    engine/obs/ObsThumbnailService.cpp
    engine/obs/ObsThumbnailService.h
    ui/AboutDialog.cpp
    ui/AboutDialog.h
    ui/AudioMeterWidget.cpp
    ui/AudioMeterWidget.h
    ui/ObsDualMediaTestWindow.cpp
    ui/ObsDualMediaTestWindow.h
    ui/ObsProgramOutputWindow.cpp
    ui/ObsProgramOutputWindow.h
    ui/ObsMediaTestWindow.cpp
    ui/ObsMediaTestWindow.h
)

add_executable(MediaSwitcher WIN32 ${SOURCES})

target_include_directories(MediaSwitcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MediaSwitcher PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
    fmt::fmt
    OBS::libobs
)

# libobs Windows resolves data and plugin paths relative to bin/64bit.
set(OBS_DEPLOY_ROOT "${CMAKE_BINARY_DIR}/obs-runtime-stage")
set_target_properties(MediaSwitcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OBS_DEPLOY_ROOT}/bin/64bit"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OBS_DEPLOY_ROOT}/bin/64bit"
)

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

add_custom_command(TARGET MediaSwitcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.ico"
        "$<TARGET_FILE_DIR:MediaSwitcher>/app_icon.ico"
)

# Deploy Qt against the executable itself so dependency scanning does not
# accidentally inspect the OBS/FFmpeg runtime DLL set.
get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
add_custom_command(TARGET MediaSwitcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${QT_BIN_DIR}\\;$ENV{PATH}"
        "${QT_BIN_DIR}/windeployqt.exe"
        --no-compiler-runtime
        --no-system-d3d-compiler
        "$<TARGET_FILE:MediaSwitcher>"
)

if(MSVC)
    target_compile_options(MediaSwitcher PRIVATE /W4)
endif()
