cmake_minimum_required(VERSION 3.28)

# waywallen-ffmpeg: shared C++20 module wrapping libav* for thumbnail and (in
# future) lightweight video-renderer use. Built as a CXX_MODULES target so its
# public API can be consumed via `import waywallen.ffmpeg;`.
#
# Depends on:
#   - rstd::rstd     (must already exist; consumer adds it via add_subdirectory
#                    or fetchdeps before adding this lib)
#   - libav* + libswscale via pkg-config

if(NOT TARGET rstd::rstd)
    message(FATAL_ERROR
        "waywallen-ffmpeg: rstd::rstd target not found. "
        "Ensure rstd is added (e.g. via fetchdeps) before this lib.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(WW_AVFORMAT REQUIRED IMPORTED_TARGET libavformat>=58)
pkg_check_modules(WW_AVCODEC  REQUIRED IMPORTED_TARGET libavcodec>=58)
pkg_check_modules(WW_AVUTIL   REQUIRED IMPORTED_TARGET libavutil>=56)
pkg_check_modules(WW_SWSCALE  REQUIRED IMPORTED_TARGET libswscale>=5)

add_library(waywallen_ffmpeg STATIC)
add_library(waywallen::ffmpeg ALIAS waywallen_ffmpeg)

target_compile_features(waywallen_ffmpeg PUBLIC cxx_std_20)
target_compile_options(waywallen_ffmpeg PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers
)

target_sources(waywallen_ffmpeg
    PUBLIC FILE_SET CXX_MODULES FILES
        src/decoder.cppm
)
target_sources(waywallen_ffmpeg PRIVATE
    src/decoder.cpp
)

target_link_libraries(waywallen_ffmpeg
    PUBLIC
        rstd::rstd
    PRIVATE
        PkgConfig::WW_AVFORMAT
        PkgConfig::WW_AVCODEC
        PkgConfig::WW_AVUTIL
        PkgConfig::WW_SWSCALE
)

set_target_properties(waywallen_ffmpeg PROPERTIES CXX_SCAN_FOR_MODULES ON)
