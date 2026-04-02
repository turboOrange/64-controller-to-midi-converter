cmake_minimum_required(VERSION 3.21)

# Pull in the Pico SDK (must be done before project())
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(n64_midi_converter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise the Pico SDK
pico_sdk_init()

# ─── libjoybus ───────────────────────────────────────────────────────────────
# Select the RP2xxx PIO backend
set(JOYBUS_BACKEND_RP2XXX 1)
add_subdirectory(lib/libjoybus)

# ─── Main firmware target ────────────────────────────────────────────────────
add_executable(n64_midi_converter
    src/main.cpp
    src/n64_controller.cpp
    src/midi.cpp
    src/mapping.cpp
)

# Generate PIO header for our custom pio programs (if any)
# pico_generate_pio_header(n64_midi_converter ${CMAKE_CURRENT_LIST_DIR}/pio/joybus.pio)

target_include_directories(n64_midi_converter PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(n64_midi_converter
    pico_stdlib
    hardware_uart
    hardware_pio
    hardware_dma
    hardware_irq
    pico_unique_id
    joybus           # loopj/libjoybus (RP2xxx PIO backend)
)

# Enable USB and UART stdio output (useful for debugging)
pico_enable_stdio_usb(n64_midi_converter 0)
pico_enable_stdio_uart(n64_midi_converter 1)

# Create UF2 / bin / hex artefacts
pico_add_extra_outputs(n64_midi_converter)

