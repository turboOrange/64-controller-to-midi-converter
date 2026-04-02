#pragma once

#include <stdint.h>

// ─── GPIO Pin Assignments ────────────────────────────────────────────────────

/// GPIO pin connected to the N64 controller data line (open-drain, 3.3 V).
constexpr uint8_t PIN_JOYBUS     = 2;

/// GPIO pin for the 5-pin DIN MIDI UART TX line.
constexpr uint8_t PIN_MIDI_TX    = 4;

/// Onboard LED (Pico pin 25, or GP25 on custom PCB).
constexpr uint8_t PIN_LED        = 25;

// ─── MIDI Settings ───────────────────────────────────────────────────────────

/// UART instance used for DIN MIDI output.
#define MIDI_UART_INSTANCE uart1

/// MIDI baud rate — fixed at 31 250 baud per MIDI 1.0 spec.
constexpr uint32_t MIDI_BAUD     = 31250;

/// Default MIDI channel (0-indexed, so channel 1 = 0x00).
constexpr uint8_t MIDI_CHANNEL   = 0x00;

/// Default MIDI program: GM #80 Ocarina (0-indexed → 79).
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

/// How often to poll the N64 controller, in milliseconds.
constexpr uint16_t POLL_INTERVAL_MS = 10;  // 100 Hz

