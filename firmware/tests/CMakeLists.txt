cmake_minimum_required(VERSION 3.21)
project(n64_midi_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Catch2 ───────────────────────────────────────────────────────────────────
# Prefer a system-installed Catch2 v3 (available when inside `nix develop`
# after flake.nix adds catch2_3).  Fall back to fetching it from GitHub if
# not present — useful for CI environments without Nix.
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found via find_package — fetching with FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.3
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# ─── Test executable ──────────────────────────────────────────────────────────
add_executable(n64_midi_tests
    test_mapping.cpp
    midi_stubs.cpp
    # Compile the real mapping logic natively — this is the code under test.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mapping.cpp
)

target_include_directories(n64_midi_tests PRIVATE
    # Stub headers must come first so they shadow any Pico SDK headers
    # that config.h would otherwise pull in (hardware/pio.h etc.)
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    # Firmware source headers: mapping.h, midi.h, n64_controller.h, config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    # libjoybus: only joybus/n64.h is used — pure #define bitmasks, no SDK deps
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libjoybus/include
    # midi_stubs.h lives next to the test sources
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(n64_midi_tests PRIVATE
    Catch2::Catch2WithMain
)

target_compile_options(n64_midi_tests PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

# ─── CTest integration ────────────────────────────────────────────────────────
include(CTest)
include(Catch)
catch_discover_tests(n64_midi_tests)

