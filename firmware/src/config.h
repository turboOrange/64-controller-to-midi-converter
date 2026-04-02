#pragma once

#include <stdint.h>
#include "hardware/pio.h"   // PIO type used in JOYBUS_PIO_INSTANCE

// ─── Multi-Controller ────────────────────────────────────────────────────────

/// Number of simultaneously supported N64 controllers (one MIDI channel each).
constexpr uint8_t NUM_CONTROLLERS = 4;

/// Joybus data-line GPIO for each controller (open-drain, 3.3 V).
/// GPIO 4 and 5 are reserved for MIDI UART TX/RX — keep clear.
constexpr uint8_t CONTROLLER_PINS[NUM_CONTROLLERS] = {2, 3, 6, 7};

/// MIDI output channel per controller (0-indexed: 0 = ch 1 … 3 = ch 4).
constexpr uint8_t CONTROLLER_CHANNELS[NUM_CONTROLLERS] = {0, 1, 2, 3};

/// PIO instance shared by all four Joybus state machines.
/// pio0 has exactly four state machines — one per controller.
#define JOYBUS_PIO_INSTANCE pio0

// ─── GPIO Pin Assignments ────────────────────────────────────────────────────

/// GPIO pin for the 5-pin DIN MIDI UART TX line (UART1 TX).
constexpr uint8_t PIN_MIDI_TX    = 4;

/// GPIO pin for the 5-pin DIN MIDI UART RX line (UART1 RX).
/// Required by midi_uart_lib even when MIDI IN is not used; leave unconnected
/// on the PCB if DIN MIDI input is not needed.
constexpr uint8_t PIN_MIDI_RX    = 5;

/// Onboard LED (Pico GP25 / custom PCB equivalent).
constexpr uint8_t PIN_LED        = 25;

// ─── MIDI Settings ───────────────────────────────────────────────────────────

/// UART instance used for DIN MIDI output.
#define MIDI_UART_INSTANCE uart1

/// MIDI baud rate — fixed at 31 250 baud per MIDI 1.0 spec.
constexpr uint32_t MIDI_BAUD     = 31250;

/// Default MIDI program per controller: GM #80 Ocarina (0-indexed → 79).
constexpr uint8_t MIDI_PROGRAM   = 79;

// ─── Note Mapping ────────────────────────────────────────────────────────────
// Root note is D4 (MIDI note 62) — matching OoT's D-major ocarina palette.

/// Root note (A button). Default: D4 = MIDI 62.
constexpr uint8_t NOTE_ROOT      = 62;  // D4

/// B button  → perfect fifth above root (A4 = MIDI 69).
constexpr uint8_t NOTE_B         = NOTE_ROOT + 7;

/// C-Up      → +2 semitones above root (E4 = MIDI 64).
constexpr uint8_t NOTE_C_UP      = NOTE_ROOT + 2;

/// C-Down    → +4 semitones above root (F#4 = MIDI 66).
constexpr uint8_t NOTE_C_DOWN    = NOTE_ROOT + 4;

/// C-Left    → +7 semitones above root (A4 = MIDI 69, different octave context).
constexpr uint8_t NOTE_C_LEFT    = NOTE_ROOT + 7;

/// C-Right   → +9 semitones above root (B4 = MIDI 71).
constexpr uint8_t NOTE_C_RIGHT   = NOTE_ROOT + 9;

// ─── Joystick Settings ───────────────────────────────────────────────────────

/// Dead-zone radius for joystick (0–127). Inputs within this radius are
/// treated as centred and produce no pitch-bend or modulation output.
constexpr uint8_t JOYSTICK_DEADZONE = 8;

/// Maximum joystick axis magnitude reported by the N64 controller (≈ ±85).
constexpr uint8_t JOYSTICK_MAX      = 85;

// ─── MIDI Clock (Real-Time) ──────────────────────────────────────────────────

/// Enable MIDI real-time clock output.
constexpr bool MIDI_CLOCK_ENABLED = false;

/// BPM for MIDI clock output when enabled.
constexpr uint16_t MIDI_CLOCK_BPM = 120;

// ─── Controller Polling ──────────────────────────────────────────────────────

/// How often each controller task polls its N64 controller, in milliseconds.
/// 10 ms = 100 Hz, matching the original OoT polling cadence.
constexpr uint16_t POLL_INTERVAL_MS = 10;

// ─── FreeRTOS Task Configuration ─────────────────────────────────────────────

/// Stack size in 32-bit words for each controller task.
/// 512 words = 2 KB — generous for Mapping state + libjoybus call overhead.
constexpr uint16_t CONTROLLER_TASK_STACK_WORDS = 512;

/// FreeRTOS priority for controller tasks.
constexpr uint8_t CONTROLLER_TASK_PRIORITY = 2;

/// Stack size in 32-bit words for the optional MIDI real-time clock task.
constexpr uint16_t MIDI_CLOCK_TASK_STACK_WORDS = 256;

/// FreeRTOS priority for the MIDI clock task.
/// Slightly higher than controller tasks so clock ticks are not delayed.
constexpr uint8_t MIDI_CLOCK_TASK_PRIORITY = 3;

