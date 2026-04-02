/**
 * @file midi_stubs.h
 * @brief MIDI event capture helpers for unit tests.
 *
 * Provides a simple FIFO log that the stub implementations of all midi_*
 * functions write into.  Tests call midi_stubs_reset() before exercising
 * mapping logic, then inspect midi_stubs_events() to verify the exact
 * sequence of MIDI messages the Mapping class would have emitted.
 *
 * None of this touches real hardware or requires FreeRTOS — the stubs
 * are pure C++ with no platform dependencies.
 */
#pragma once

#include <cstdint>
#include <vector>

/**
 * @brief One captured MIDI event, tagged by type.
 *
 * Byte fields use the names most natural for each message type; unused
 * fields are set to 0.  See individual comments below.
 */
struct MidiEvent {
    enum class Type : uint8_t {
        NoteOn,         ///< midi_note_on()
        NoteOff,        ///< midi_note_off()
        ControlChange,  ///< midi_control_change() (also from midi_panic())
        ProgramChange,  ///< midi_program_change()
        PitchBend,      ///< midi_pitch_bend()
        Realtime,       ///< midi_realtime()
    };

    Type    type;
    uint8_t channel;  ///< MIDI channel 0–15
    uint8_t b1;       ///< note / cc / program / realtime byte
    uint8_t b2;       ///< velocity / cc-value / 0
    int16_t bend;     ///< signed bend value (PitchBend only; –8192…+8191)
};

/**
 * @brief Clear the captured event log.  Call at the start of each test case.
 */
void midi_stubs_reset();

/**
 * @brief Return a snapshot of all events logged since the last reset.
 *
 * The returned vector is in chronological order (oldest first).
 */
const std::vector<MidiEvent> &midi_stubs_events();

/**
 * @brief Convenience: return the number of events in the log.
 */
std::size_t midi_stubs_count();

