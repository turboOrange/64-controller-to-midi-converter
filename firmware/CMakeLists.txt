cmake_minimum_required(VERSION 3.21)

# Pull in the Pico SDK (must be done before project())
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(n64_midi_converter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Default board — override on the cmake command line if using a custom PCB:
#   cmake -DPICO_BOARD=my_board ..
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico)
endif()

# Initialise the Pico SDK
pico_sdk_init()

# ─── libjoybus ───────────────────────────────────────────────────────────────
# Select the RP2xxx PIO backend (must be CACHE INTERNAL to propagate into the
# subdirectory scope where libjoybus' CMakeLists.txt checks DEFINED).
set(JOYBUS_BACKEND_RP2XXX 1 CACHE INTERNAL "")
add_subdirectory(lib/libjoybus)

# ─── rppicomidi/ring_buffer_lib + midi_uart_lib ───────────────────────────────
# ring_buffer_lib must be added before midi_uart_lib because midi_uart_lib's
# own CMakeLists.txt does add_subdirectory(../ring_buffer_lib ...) using a
# relative path; adding it here first satisfies the if(NOT TARGET) guard and
# avoids a duplicate-target error.
add_subdirectory(lib/ring_buffer_lib)
add_subdirectory(lib/midi_uart_lib)

# ─── Main firmware target ────────────────────────────────────────────────────
add_executable(n64_midi_converter
    src/main.cpp
    src/n64_controller.cpp
    src/midi.cpp
    src/mapping.cpp
)

target_include_directories(n64_midi_converter PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src   # also provides midi_uart_lib_config.h
)

target_link_libraries(n64_midi_converter
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_irq
    pico_unique_id
    joybus           # loopj/libjoybus (RP2xxx PIO backend)
    midi_uart_lib    # rppicomidi/midi_uart_lib (IRQ-driven UART MIDI TX/RX)
    ring_buffer_lib  # rppicomidi/ring_buffer_lib (required by midi_uart_lib)
)

# USB stdio off; UART stdio off — UART1 is fully owned by midi_uart_lib.
pico_enable_stdio_usb(n64_midi_converter 0)
pico_enable_stdio_uart(n64_midi_converter 0)

# Create UF2 / bin / hex artefacts
pico_add_extra_outputs(n64_midi_converter)
