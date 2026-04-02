cmake_minimum_required(VERSION 3.21)
project(n64_midi_tester CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use pkg-config to locate ncurses — this works correctly in Nix store
# environments where CMake's FindCurses module cannot resolve the library path.
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURSES REQUIRED ncurses)

add_executable(n64_midi_tester
    main.cpp
    # Compile the real mapping logic natively — this is the code under test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/mapping.cpp
)

target_include_directories(n64_midi_tester PRIVATE
    # Must come first: stub headers shadow the real Pico SDK headers
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    # Firmware source headers (mapping.h, midi.h, n64_controller.h, config.h)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    # libjoybus: only joybus/n64.h is used here — pure #define bitmasks, no SDK deps
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/libjoybus/include
    ${CURSES_INCLUDE_DIRS}
)

target_link_libraries(n64_midi_tester PRIVATE
    ${CURSES_LIBRARIES}
)

target_compile_options(n64_midi_tester PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

