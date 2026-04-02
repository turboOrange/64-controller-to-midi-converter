#pragma once

#include <stdint.h>
#include <stdbool.h>


/**
 * @brief Snapshot of a single N64 controller poll.
 *
 * Mirrors `joybus_n64_controller_input` but is re-exported as a clean C++
 * struct so the rest of the firmware never needs to include libjoybus headers
 * directly.
 */
struct N64State {
    uint16_t buttons;   ///< Bitmask of pressed buttons (see JOYBUS_N64_BUTTON_* constants).
    int8_t   stick_x;   ///< Joystick X axis, signed –128…+127 (positive = right).
    int8_t   stick_y;   ///< Joystick Y axis, signed –128…+127 (positive = up).
};

/**
 * @brief Initialise the N64 Joybus interface on the configured GPIO/PIO.
 *
 * Must be called once before any polling. Uses PIN_JOYBUS from config.h and
 * pio0. The RP2xxx PIO backend handles all timing-critical Joybus signalling.
 *
 * @return true on success, false if PIO resources could not be claimed.
 */
bool n64_controller_init();

/**
 * @brief Poll the N64 controller and update the provided state struct.
 *
 * This is a synchronous wrapper around the async libjoybus API, suitable for
 * use in a simple super-loop. Should be called at POLL_INTERVAL_MS cadence.
 *
 * @param[out] state  Destination for the latest controller state.
 * @return true if a valid response was received, false on Joybus error.
 */
bool n64_controller_poll(N64State &state);

