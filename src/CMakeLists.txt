# Find Packages
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# DirectX 11 Libraries
set(DIRECTX_LIBRARIES d3d11.lib dxgi.lib d3dcompiler.lib)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Source files
set(SOURCES
    main.cpp
    common/logger/Logger.cpp
    common/logger/Logger.h
    common/config/Config.cpp
    common/config/Config.h
    app/WorkspaceManager.cpp
    app/WorkspaceManager.h
    ui/MainWindow.cpp
    ui/MainWindow.h
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
    engine/input/ColorBarsSource.cpp
    engine/input/ColorBarsSource.h
)

# Qt UI Files (if any)
# set(UI_FILES ui/MainWindow.ui)
# qt6_wrap_ui(UI_SOURCES ${UI_FILES})

# MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Executable
add_executable(MediaSwitcher WIN32 ${SOURCES})

# Link libraries
target_link_libraries(MediaSwitcher PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
    fmt::fmt
    ${DIRECTX_LIBRARIES}
)

# Compile options
if(MSVC)
    target_compile_options(MediaSwitcher PRIVATE /W4)
endif()
