set(OBS_REQUIRED_VERSION "32.1.2")

if (NOT OBS_RUNTIME_ROOT)
    message(FATAL_ERROR "MEDIASWITCHER_ENABLE_OBS=ON requires OBS_RUNTIME_ROOT to point to a controlled OBS ${OBS_REQUIRED_VERSION} Windows x64 runtime.")
endif()

get_filename_component(OBS_RUNTIME_ROOT "${OBS_RUNTIME_ROOT}" ABSOLUTE)
set(OBS_RUNTIME_MANIFEST "${OBS_RUNTIME_ROOT}/runtime-manifest.json")
if (NOT EXISTS "${OBS_RUNTIME_MANIFEST}")
    message(FATAL_ERROR "OBS runtime is missing '${OBS_RUNTIME_MANIFEST}'. Create it with tools/PrepareObsRuntime.ps1; arbitrary installed OBS directories are intentionally unsupported.")
endif()

file(READ "${OBS_RUNTIME_MANIFEST}" OBS_RUNTIME_MANIFEST_CONTENT)
string(JSON OBS_RUNTIME_VERSION ERROR_VARIABLE OBS_RUNTIME_VERSION_ERROR GET "${OBS_RUNTIME_MANIFEST_CONTENT}" version)
string(JSON OBS_RUNTIME_ARCH ERROR_VARIABLE OBS_RUNTIME_ARCH_ERROR GET "${OBS_RUNTIME_MANIFEST_CONTENT}" architecture)
if (OBS_RUNTIME_VERSION_ERROR OR NOT OBS_RUNTIME_VERSION STREQUAL OBS_REQUIRED_VERSION)
    message(FATAL_ERROR "OBS runtime version must be ${OBS_REQUIRED_VERSION}; manifest reports '${OBS_RUNTIME_VERSION}'.")
endif()
if (OBS_RUNTIME_ARCH_ERROR OR NOT OBS_RUNTIME_ARCH STREQUAL "windows-x64")
    message(FATAL_ERROR "OBS runtime architecture must be windows-x64; manifest reports '${OBS_RUNTIME_ARCH}'.")
endif()

set(OBS_RUNTIME_BIN_DIR "${OBS_RUNTIME_ROOT}/bin/64bit")
set(OBS_RUNTIME_PLUGIN_BIN_DIR "${OBS_RUNTIME_ROOT}/obs-plugins/64bit")
set(OBS_RUNTIME_PLUGIN_DATA_DIR "${OBS_RUNTIME_ROOT}/data/obs-plugins")
set(OBS_RUNTIME_CORE_DATA_DIR "${OBS_RUNTIME_ROOT}/data/libobs")

find_path(OBS_INCLUDE_DIR NAMES obs.h obsconfig.h HINTS "${OBS_RUNTIME_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
find_library(OBS_LIBRARY NAMES obs HINTS "${OBS_RUNTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)

foreach(required_directory OBS_RUNTIME_BIN_DIR OBS_RUNTIME_PLUGIN_BIN_DIR OBS_RUNTIME_PLUGIN_DATA_DIR OBS_RUNTIME_CORE_DATA_DIR)
    if (NOT IS_DIRECTORY "${${required_directory}}")
        message(FATAL_ERROR "OBS runtime is incomplete: '${${required_directory}}' does not exist.")
    endif()
endforeach()

set(OBS_REQUIRED_BINARIES
    obs.dll
    libobs-d3d11.dll
    avcodec-61.dll
    avdevice-61.dll
    avfilter-10.dll
    avformat-61.dll
    avutil-59.dll
    swresample-5.dll
    swscale-8.dll
    libx264-164.dll
    zlib.dll
    w32-pthreads.dll
    librist.dll
    srt.dll
)
foreach(binary IN LISTS OBS_REQUIRED_BINARIES)
    if (NOT EXISTS "${OBS_RUNTIME_BIN_DIR}/${binary}")
        message(FATAL_ERROR "OBS runtime ${OBS_REQUIRED_VERSION} is missing required binary '${OBS_RUNTIME_BIN_DIR}/${binary}'.")
    endif()
endforeach()

if (NOT EXISTS "${OBS_RUNTIME_PLUGIN_BIN_DIR}/obs-ffmpeg.dll")
    message(FATAL_ERROR "OBS runtime ${OBS_REQUIRED_VERSION} is missing required media module '${OBS_RUNTIME_PLUGIN_BIN_DIR}/obs-ffmpeg.dll'.")
endif()
if (NOT IS_DIRECTORY "${OBS_RUNTIME_PLUGIN_DATA_DIR}/obs-ffmpeg")
    message(FATAL_ERROR "OBS runtime ${OBS_REQUIRED_VERSION} is missing required plugin data '${OBS_RUNTIME_PLUGIN_DATA_DIR}/obs-ffmpeg'.")
endif()
if (NOT EXISTS "${OBS_RUNTIME_CORE_DATA_DIR}/default.effect")
    message(FATAL_ERROR "OBS runtime ${OBS_REQUIRED_VERSION} is missing required libobs effect '${OBS_RUNTIME_CORE_DATA_DIR}/default.effect'.")
endif()

add_library(OBS::libobs SHARED IMPORTED GLOBAL)
set_target_properties(OBS::libobs PROPERTIES
    IMPORTED_IMPLIB "${OBS_LIBRARY}"
    IMPORTED_LOCATION "${OBS_RUNTIME_BIN_DIR}/obs.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${OBS_INCLUDE_DIR}"
)

message(STATUS "MediaSwitcher OBS runtime: ${OBS_RUNTIME_ROOT}")
message(STATUS "MediaSwitcher OBS runtime version: ${OBS_RUNTIME_VERSION} (${OBS_RUNTIME_ARCH})")
message(STATUS "MediaSwitcher OBS modules: obs-ffmpeg; graphics backend: libobs-d3d11")