option(WAYWALLEN_BUILD_IMAGE_PLUGIN "Build the ffmpeg+Vulkan image plugin" ON)
option(WAYWALLEN_BUILD_VIDEO_PLUGIN "Build the ffmpeg+Vulkan video plugin" ON)
option(WAYWALLEN_BUILD_MPV_PLUGIN   "Build the libmpv+EGL/GBM video plugin" OFF)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
include(${CMAKE_SOURCE_DIR}/cmake/FetchDeps.cmake)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
fetchdeps(${CMAKE_SOURCE_DIR}/deps.json NAMES rstd wavsen nlohmann_json)

add_subdirectory(../bridge waywallen-bridge-build)

add_library(waywallen-plugin-json STATIC)
target_sources(waywallen-plugin-json
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        nlohmann_json.cppm
)
target_link_libraries(waywallen-plugin-json PUBLIC nlohmann_json::nlohmann_json)
set_target_properties(waywallen-plugin-json PROPERTIES CXX_SCAN_FOR_MODULES ON)

if(WAYWALLEN_BUILD_IMAGE_PLUGIN)
    add_subdirectory(org.waywallen.image)
endif()

if(WAYWALLEN_BUILD_VIDEO_PLUGIN)
    add_subdirectory(org.waywallen.video)
endif()

if(WAYWALLEN_BUILD_MPV_PLUGIN)
    add_subdirectory(mpv)
endif()
