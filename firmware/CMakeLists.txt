cmake_minimum_required(VERSION 3.21)

# ─── SDK path ─────────────────────────────────────────────────────────────────
# Support both the environment variable (Nix dev-shell / CI) and an explicit
# -DPICO_SDK_PATH=... cmake argument for IDE / out-of-shell invocations.
if (DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(ENV{PICO_SDK_PATH} "${PICO_SDK_PATH}")
endif ()

# Pull in the Pico SDK (must be before project())
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(n64_midi_converter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico)
endif()

# Initialise the Pico SDK — sets PICO_PLATFORM to "rp2040"
pico_sdk_init()

# ─── FreeRTOS-Kernel ─────────────────────────────────────────────────────────
# Submodule at firmware/lib/FreeRTOS-Kernel (RP2040 SMP port, V11.0.1+).
# We use add_subdirectory directly after pico_sdk_init() so that PICO_PLATFORM
# is already "rp2040" and the port's own pico_sdk_import.cmake guard fires
# correctly (PICO_SDK_PATH cmake variable is already set by pico_sdk_init()).
# FreeRTOSConfig.h lives in src/ — passed via FREERTOS_CONFIG_FILE_DIRECTORY.
set(FREERTOS_KERNEL_PATH      ${CMAKE_CURRENT_LIST_DIR}/lib/FreeRTOS-Kernel)
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/src CACHE PATH
    "Directory that contains FreeRTOSConfig.h")
add_subdirectory(
    ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040
    FREERTOS_KERNEL
)

# ─── libjoybus ───────────────────────────────────────────────────────────────
set(JOYBUS_BACKEND_RP2XXX 1 CACHE INTERNAL "")
add_subdirectory(lib/libjoybus)

# ─── rppicomidi/ring_buffer_lib + midi_uart_lib ───────────────────────────────
add_subdirectory(lib/ring_buffer_lib)
add_subdirectory(lib/midi_uart_lib)

# ─── Main firmware target ────────────────────────────────────────────────────
add_executable(n64_midi_converter
    src/main.cpp
    src/n64_controller.cpp
    src/midi.cpp
    src/mapping.cpp
    src/controller_task.cpp
)

target_include_directories(n64_midi_converter PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src   # FreeRTOSConfig.h + midi_uart_lib_config.h + ring_buffer_lib_config.h
)

target_link_libraries(n64_midi_converter
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_irq
    hardware_gpio
    pico_unique_id
    joybus                 # loopj/libjoybus (RP2xxx PIO backend)
    midi_uart_lib          # rppicomidi/midi_uart_lib (IRQ-driven UART MIDI TX/RX)
    ring_buffer_lib        # rppicomidi/ring_buffer_lib (required by midi_uart_lib)
    FreeRTOS-Kernel-Static # RP2040 SMP FreeRTOS + configSUPPORT_STATIC_ALLOCATION
    FreeRTOS-Kernel-Heap4  # heap_4 fallback allocator
)

# USB stdio off; UART stdio off — UART1 is fully owned by midi_uart_lib.
pico_enable_stdio_usb(n64_midi_converter 0)
pico_enable_stdio_uart(n64_midi_converter 0)

pico_add_extra_outputs(n64_midi_converter)

# ─── Simulation build (Renode) ────────────────────────────────────────────────
# Build with:  cmake -B build_sim -G Ninja -DSIMULATION=1
#              ninja -C build_sim
# Produces:   build_sim/n64_midi_converter_sim.elf  (loaded by Renode)
#
# Key differences from the production build:
#   • n64_controller_sim.cpp replaces n64_controller.cpp (no PIO/DMA/Joybus)
#   • -DSIMULATION=1 triggers single-core FreeRTOS in FreeRTOSConfig.h
#   • hardware_pio / hardware_dma / joybus are not linked (not needed)
if(SIMULATION)
    add_executable(n64_midi_converter_sim
        src/main.cpp
        src/n64_controller_sim.cpp   # stub: reads N64State from SRAM @ 0x20040000
        src/midi.cpp
        src/mapping.cpp
        src/controller_task.cpp
        src/sim_overrides.c          # no-op overrides for SDK __weak init funcs
    )
    target_compile_definitions(n64_midi_converter_sim PRIVATE SIMULATION=1)
    target_include_directories(n64_midi_converter_sim PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        # joybus/n64.h is needed by mapping.cpp for the button bitmask #defines.
        # We only need the headers — no PIO backend is compiled.
        ${CMAKE_CURRENT_LIST_DIR}/lib/libjoybus/include
    )
    target_link_libraries(n64_midi_converter_sim
        pico_stdlib
        hardware_pio        # header-only — satisfies config.h's #include "hardware/pio.h"
        hardware_uart
        hardware_irq
        hardware_gpio
        pico_unique_id
        midi_uart_lib
        ring_buffer_lib
        FreeRTOS-Kernel-Static
        FreeRTOS-Kernel-Heap4
    )
    pico_enable_stdio_usb(n64_midi_converter_sim 0)
    pico_enable_stdio_uart(n64_midi_converter_sim 0)
    pico_add_extra_outputs(n64_midi_converter_sim)
endif()


