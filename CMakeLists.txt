cmake_minimum_required(VERSION 3.28)
project(waywallen
    VERSION 0.1.8
    LANGUAGES C CXX
    DESCRIPTION "waywallen: Rust daemon + Qt/QML UI + renderer subprocess plugins"
)

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)


set(WAYWALLEN_LIB_RPATH "$ORIGIN")
# All ELF binaries (daemon + renderer plugins) install to <prefix>/bin —
# share/ ELF trips permission limits on some distros. $ORIGIN/../lib
# reaches the shared libs in <prefix>/lib.
set(WAYWALLEN_BIN_RPATH "$ORIGIN;$ORIGIN/../lib")

include(GNUInstallDirs)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(WAYWALLEN_BUILD_DAEMON  "Build the Rust daemon via Corrosion"   ON)
option(WAYWALLEN_BUILD_UI      "Build the Qt/QML UI"                   ON)
option(WAYWALLEN_BUILD_PLUGINS "Build renderer plugins (image, video)" ON)
option(WAYWALLEN_CARGO_OFFLINE "Pass --frozen to cargo (offline + locked, for flatpak)" OFF)

if(WAYWALLEN_BUILD_DAEMON)
    include(FetchDeps)
    fetchdeps(${CMAKE_CURRENT_SOURCE_DIR}/deps.json NAMES Corrosion)
    # FROZEN = --locked --offline; requires a committed/staged Cargo.lock
    # and a vendored CARGO_HOME (set by the flatpak manifest).
    set(_waywallen_corrosion_extra "")
    if(WAYWALLEN_CARGO_OFFLINE)
        list(APPEND _waywallen_corrosion_extra FROZEN)
    endif()
    corrosion_import_crate(
        MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
        CRATES        waywallen
        ${_waywallen_corrosion_extra}
    )
    install(IMPORTED_RUNTIME_ARTIFACTS
        waywallen
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Daemon
    )

    find_program(CARGO cargo REQUIRED)
    set(_waywallen_display_dir "${CMAKE_CURRENT_SOURCE_DIR}/../waywallen-display")
    if(NOT EXISTS "${_waywallen_display_dir}/Cargo.toml")
        message(FATAL_ERROR "waywallen-display Cargo.toml not found at ${_waywallen_display_dir}")
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        set(_waywallen_display_cargo_profile "release")
        set(_waywallen_display_cargo_profile_arg "--release")
    else()
        set(_waywallen_display_cargo_profile "debug")
        set(_waywallen_display_cargo_profile_arg "")
    endif()
    set(_waywallen_layer_shell_bin
        "${_waywallen_display_dir}/target/${_waywallen_display_cargo_profile}/waywallen-layer-shell")
    set(_waywallen_display_cargo_args build --bin waywallen-layer-shell)
    if(_waywallen_display_cargo_profile_arg)
        list(APPEND _waywallen_display_cargo_args "${_waywallen_display_cargo_profile_arg}")
    endif()
    if(WAYWALLEN_CARGO_OFFLINE)
        list(APPEND _waywallen_display_cargo_args --frozen)
    endif()

    add_custom_command(
        OUTPUT "${_waywallen_layer_shell_bin}"
        COMMAND ${CARGO} ${_waywallen_display_cargo_args}
        WORKING_DIRECTORY "${_waywallen_display_dir}"
        DEPENDS "${_waywallen_display_dir}/Cargo.toml"
        COMMENT "Building waywallen-layer-shell"
        VERBATIM)
    add_custom_target(waywallen-layer-shell ALL
        DEPENDS "${_waywallen_layer_shell_bin}")
    install(PROGRAMS "${_waywallen_layer_shell_bin}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Daemon)
endif()

if(WAYWALLEN_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

if(WAYWALLEN_BUILD_UI)
    add_subdirectory(ui)
endif()

message(STATUS
    "waywallen components: daemon=${WAYWALLEN_BUILD_DAEMON} ui=${WAYWALLEN_BUILD_UI} plugins=${WAYWALLEN_BUILD_PLUGINS}")

include(CPackConfig)
