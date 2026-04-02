#pragma once

#include <stdint.h>
#include <stdbool.h>

// ─── MIDI message types ───────────────────────────────────────────────────────

/// Status byte prefixes (upper nibble), lower nibble is channel 0–15.
enum class MidiStatus : uint8_t {
    NoteOff      = 0x80,
    NoteOn       = 0x90,
    PolyPressure = 0xA0,
    ControlChange= 0xB0,
    ProgramChange= 0xC0,
    ChannelPress = 0xD0,
    PitchBend    = 0xE0,
};

/// Commonly-used MIDI CC numbers.
enum class MidiCC : uint8_t {
    Modulation   = 1,
    SustainPedal = 64,
    AllNotesOff  = 123,
};

/// Single-byte MIDI system real-time messages.
enum class MidiRealTime : uint8_t {
    Clock      = 0xF8,
    Start      = 0xFA,
    Continue   = 0xFB,
    Stop       = 0xFC,
    ActiveSens = 0xFE,
    Reset      = 0xFF,
};

// ─── Public API ───────────────────────────────────────────────────────────────

/**
 * @brief Initialise MIDI UART output at 31 250 baud.
 *
 * Configures the UART defined by MIDI_UART_INSTANCE / PIN_MIDI_TX in
 * config.h and sends an initial Program Change to select GM #80 (Ocarina).
 */
void midi_init();

/**
 * @brief Send a Note On message.
 * @param channel  MIDI channel (0–15).
 * @param note     MIDI note number (0–127).
 * @param velocity Note velocity (1–127). A value of 0 is treated as Note Off.
 */
void midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);

/**
 * @brief Send a Note Off message.
 * @param channel  MIDI channel (0–15).
 * @param note     MIDI note number (0–127).
 */
void midi_note_off(uint8_t channel, uint8_t note);

/**
 * @brief Send a Control Change message.
 * @param channel  MIDI channel (0–15).
 * @param cc       Controller number (0–127).
 * @param value    Controller value (0–127).
 */
void midi_control_change(uint8_t channel, uint8_t cc, uint8_t value);

/**
 * @brief Send a Program Change message.
 * @param channel  MIDI channel (0–15).
 * @param program  Program number (0–127, GM convention).
 */
void midi_program_change(uint8_t channel, uint8_t program);

/**
 * @brief Send a 14-bit Pitch Bend message.
 *
 * @param channel  MIDI channel (0–15).
 * @param bend     Signed bend value –8192…+8191 (0 = centre/no bend).
 */
void midi_pitch_bend(uint8_t channel, int16_t bend);

/**
 * @brief Send All Notes Off (CC 123, value 0) on the given channel.
 * @param channel  MIDI channel (0–15).
 */
void midi_panic(uint8_t channel);

/**
 * @brief Send a single-byte MIDI real-time message (clock tick, etc.).
 * @param msg  One of the MidiRealTime byte values.
 */
void midi_realtime(MidiRealTime msg);

