# Pulls Corrosion (https://github.com/corrosion-rs/corrosion) at a pinned tag
# so we can drive Cargo from CMake. Bump CORROSION_VERSION to update.

include_guard(GLOBAL)

set(CORROSION_VERSION "v0.6.1" CACHE STRING
    "Corrosion git tag to fetch (https://github.com/corrosion-rs/corrosion/releases)")

include(FetchContent)
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG        ${CORROSION_VERSION}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Corrosion)
