cmake_minimum_required(VERSION 3.20)

project(MediaSwitcher VERSION 1.0.0 LANGUAGES CXX)

option(MEDIASWITCHER_ENABLE_OBS "Enable the optional libobs foundation backend" OFF)
set(OBS_RUNTIME_ROOT "" CACHE PATH "Controlled libobs runtime root")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    add_compile_options(/Zm1000 /bigobj)
endif()

# Setup Output Directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# FetchContent for spdlog and fmt
include(FetchContent)

if (MEDIASWITCHER_ENABLE_OBS)
    include(cmake/FindObsRuntime.cmake)
endif()

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.1.1
)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.12.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    ffmpeg
    URL https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip
)
FetchContent_MakeAvailable(ffmpeg)

# Locate FFmpeg headers and libraries inside extracted directory
file(GLOB FFMPEG_SUBDIRS "${ffmpeg_SOURCE_DIR}/ffmpeg-*")
if(FFMPEG_SUBDIRS)
    list(GET FFMPEG_SUBDIRS 0 FFMPEG_ROOT_DIR)
else()
    set(FFMPEG_ROOT_DIR ${ffmpeg_SOURCE_DIR})
endif()

set(FFMPEG_INCLUDE_DIR ${FFMPEG_ROOT_DIR}/include CACHE PATH "FFmpeg include dir")
set(FFMPEG_LIB_DIR ${FFMPEG_ROOT_DIR}/lib CACHE PATH "FFmpeg lib dir")
set(FFMPEG_BIN_DIR ${FFMPEG_ROOT_DIR}/bin CACHE PATH "FFmpeg bin dir")

# Add subdirectories
add_subdirectory(src)

