#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/**
 * @brief Snapshot of a single N64 controller poll.
 *
 * Mirrors `joybus_n64_controller_input` but is re-exported as a clean C++
 * struct so the rest of the firmware never needs to include libjoybus headers
 * directly.
 */
struct N64State {
    uint16_t buttons;   ///< Bitmask of pressed buttons (JOYBUS_N64_BUTTON_* constants).
    int8_t   stick_x;   ///< Joystick X axis, signed –128…+127 (positive = right).
    int8_t   stick_y;   ///< Joystick Y axis, signed –128…+127 (positive = up).
};

/**
 * @brief Initialise one N64 Joybus interface on its configured GPIO/PIO.
 *
 * Must be called once per controller index, sequentially from a single core
 * before the FreeRTOS scheduler starts. Uses CONTROLLER_PINS[idx] and
 * JOYBUS_PIO_INSTANCE from config.h; each call claims one free PIO state
 * machine from the shared PIO instance.
 *
 * @param idx  Controller index (0 … NUM_CONTROLLERS-1).
 * @return true on success, false if PIO resources could not be claimed.
 */
bool n64_controller_init(uint8_t idx);

/**
 * @brief Poll one N64 controller and update the provided state struct.
 *
 * Designed to be called from a FreeRTOS task at POLL_INTERVAL_MS cadence.
 * Instead of spin-waiting, this function blocks the calling task via a
 * FreeRTOS task notification until the PIO IRQ signals transfer completion
 * (or a 5 ms safety timeout elapses).
 *
 * @param idx    Controller index (0 … NUM_CONTROLLERS-1).
 * @param state  Destination for the latest controller state.
 * @return true if a valid response was received, false on error or timeout.
 */
bool n64_controller_poll(uint8_t idx, N64State &state);
