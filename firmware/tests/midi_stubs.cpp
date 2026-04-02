/**
 * @file midi_stubs.cpp
 * @brief Stub implementations of all midi_* functions for unit testing.
 *
 * Replaces midi.cpp entirely in the test build.  Every call is recorded in
 * a global vector which tests can inspect via the midi_stubs_* helpers
 * declared in midi_stubs.h.
 *
 * midi_panic() chains to midi_control_change() — exactly as the real
 * midi.cpp does — so the log always reflects the externally observable
 * MIDI byte stream rather than implementation-level call counts.
 *
 * No FreeRTOS, no UART, no hardware dependencies of any kind.
 */

#include "midi_stubs.h"
#include "midi.h"

#include <cstdint>

// ─── Private log ──────────────────────────────────────────────────────────────

static std::vector<MidiEvent> s_events;

// ─── Public helpers ───────────────────────────────────────────────────────────

void midi_stubs_reset()
{
    s_events.clear();
}

const std::vector<MidiEvent> &midi_stubs_events()
{
    return s_events;
}

std::size_t midi_stubs_count()
{
    return s_events.size();
}

// ─── midi_* stub implementations ─────────────────────────────────────────────

void midi_init()
{
    // No-op: the test harness initialises the Mapping object directly.
    // We intentionally do not log a ProgramChange here to keep test logs
    // uncluttered — tests that care about PC messages assert them explicitly.
}

void midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    s_events.push_back({MidiEvent::Type::NoteOn, channel, note, velocity, 0});
}

void midi_note_off(uint8_t channel, uint8_t note)
{
    s_events.push_back({MidiEvent::Type::NoteOff, channel, note, 0, 0});
}

void midi_control_change(uint8_t channel, uint8_t cc, uint8_t value)
{
    s_events.push_back({MidiEvent::Type::ControlChange, channel, cc, value, 0});
}

void midi_program_change(uint8_t channel, uint8_t program)
{
    s_events.push_back({MidiEvent::Type::ProgramChange, channel, program, 0, 0});
}

void midi_pitch_bend(uint8_t channel, int16_t bend)
{
    // Encode the signed bend value into b1/b2 as MIDI 14-bit LSB/MSB so that
    // raw-byte tests can verify the wire encoding if needed.
    const uint16_t raw = static_cast<uint16_t>(bend + 8192);
    s_events.push_back({
        MidiEvent::Type::PitchBend,
        channel,
        static_cast<uint8_t>(raw & 0x7F),         // LSB
        static_cast<uint8_t>((raw >> 7) & 0x7F),  // MSB
        bend                                        // convenient signed value
    });
}

void midi_panic(uint8_t channel)
{
    // Mirror the real midi.cpp implementation: AllNotesOff is a Control Change.
    midi_control_change(channel,
                        static_cast<uint8_t>(MidiCC::AllNotesOff),
                        0x00);
}

void midi_realtime(MidiRealTime msg)
{
    s_events.push_back({
        MidiEvent::Type::Realtime,
        0,
        static_cast<uint8_t>(msg),
        0,
        0
    });
}

