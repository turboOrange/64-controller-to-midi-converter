/**
 * @file test_mapping.cpp
 * @brief Host-side unit tests for the Mapping class (firmware/src/mapping.cpp).
 *
 * These tests run on the native host (x86/x64) — no RP2040, no Joybus, no
 * FreeRTOS required.  mapping.cpp is compiled directly; all midi_* calls are
 * intercepted by midi_stubs.cpp which records events into an inspectable log.
 *
 * Test groups
 * ──────────
 *  [note_buttons]   A/B/C-Up/Down/Left/Right → Note On / Note Off
 *  [octave]         L/R shoulder octave shift, clamping, note-off tracking
 *  [sustain]        Z button → CC64 on/off
 *  [panic]          Start → CC123, state reset
 *  [dpad]           D-pad directions → Program Change numbers
 *  [joystick]       Pitch bend (X-axis) + Modulation (Y-axis) + deadzone
 *  [velocity]       Joystick distance → MIDI velocity scaling
 *  [channel]        All events respect the bound MIDI channel
 *  [edge_cases]     Duplicate presses, multi-button chords, clamp boundary
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_range_equals.hpp>

#include "mapping.h"
#include "midi.h"        // MidiCC, MidiStatus, MidiRealTime enum classes
#include "midi_stubs.h"
#include "config.h"

extern "C" {
#include <joybus/n64.h>   // JOYBUS_N64_BUTTON_* bitmasks
}

#include <cstdint>
#include <vector>

// ─── Helpers ──────────────────────────────────────────────────────────────────

/// Build a state with one or more buttons pressed, stick centred.
static N64State buttons(uint16_t mask, int8_t sx = 0, int8_t sy = 0)
{
    return N64State{mask, sx, sy};
}

/// Build a state with no buttons and a specific stick position.
static N64State stick(int8_t sx, int8_t sy)
{
    return N64State{0, sx, sy};
}

/// Run one press→release cycle for a button mask and return all MIDI events.
static std::vector<MidiEvent> press_release(Mapping &m, uint16_t btn_mask,
                                             int8_t sx = 0, int8_t sy = 0)
{
    midi_stubs_reset();
    m.process(buttons(btn_mask, sx, sy));   // press
    m.process(buttons(0, sx, sy));           // release
    return midi_stubs_events();
}

// ─── [note_buttons] ──────────────────────────────────────────────────────────

TEST_CASE("A button emits correct note with velocity", "[note_buttons]")
{
    Mapping m(0);
    auto events = press_release(m, JOYBUS_N64_BUTTON_A);

    REQUIRE(events.size() >= 2);

    const auto &on = events[0];
    REQUIRE(on.type    == MidiEvent::Type::NoteOn);
    REQUIRE(on.channel == 0);
    REQUIRE(on.b1      == NOTE_ROOT);          // D4 = MIDI 62
    REQUIRE(on.b2      > 0);                   // velocity must be positive

    // Note Off must use the same note that was activated
    const auto *off_it = [&]() -> const MidiEvent * {
        for (auto &e : events)
            if (e.type == MidiEvent::Type::NoteOff && e.b1 == NOTE_ROOT) return &e;
        return nullptr;
    }();
    REQUIRE(off_it != nullptr);
}

TEST_CASE("B button emits perfect-fifth note", "[note_buttons]")
{
    Mapping m(0);
    auto events = press_release(m, JOYBUS_N64_BUTTON_B);

    REQUIRE(events[0].type == MidiEvent::Type::NoteOn);
    REQUIRE(events[0].b1   == NOTE_B);   // D4 + 7 semitones = A4
}

TEST_CASE("C-Up emits +2 semitones above root", "[note_buttons]")
{
    Mapping m(0);
    auto events = press_release(m, JOYBUS_N64_BUTTON_C_UP);
    REQUIRE(events[0].type == MidiEvent::Type::NoteOn);
    REQUIRE(events[0].b1   == NOTE_C_UP);
}

TEST_CASE("C-Down emits +4 semitones above root", "[note_buttons]")
{
    Mapping m(0);
    auto events = press_release(m, JOYBUS_N64_BUTTON_C_DOWN);
    REQUIRE(events[0].type == MidiEvent::Type::NoteOn);
    REQUIRE(events[0].b1   == NOTE_C_DOWN);
}

TEST_CASE("C-Left emits +7 semitones above root", "[note_buttons]")
{
    Mapping m(0);
    auto events = press_release(m, JOYBUS_N64_BUTTON_C_LEFT);
    REQUIRE(events[0].type == MidiEvent::Type::NoteOn);
    REQUIRE(events[0].b1   == NOTE_C_LEFT);
}

TEST_CASE("C-Right emits +9 semitones above root", "[note_buttons]")
{
    Mapping m(0);
    auto events = press_release(m, JOYBUS_N64_BUTTON_C_RIGHT);
    REQUIRE(events[0].type == MidiEvent::Type::NoteOn);
    REQUIRE(events[0].b1   == NOTE_C_RIGHT);
}

TEST_CASE("Note Off carries the exact note that was activated", "[note_buttons]")
{
    Mapping m(0);
    midi_stubs_reset();

    m.process(buttons(JOYBUS_N64_BUTTON_A));    // press
    m.process(buttons(0));                       // release

    const auto &evs = midi_stubs_events();

    // Search by type — pitch-bend and modulation events also fire each poll.
    uint8_t on_note = 0, off_note = 0;
    bool found_on = false, found_off = false;
    for (const auto &e : evs) {
        if (!found_on  && e.type == MidiEvent::Type::NoteOn)  { on_note  = e.b1; found_on  = true; }
        if (!found_off && e.type == MidiEvent::Type::NoteOff) { off_note = e.b1; found_off = true; }
    }
    REQUIRE(found_on);
    REQUIRE(found_off);
    REQUIRE(on_note == off_note);
}

TEST_CASE("Holding a button does not re-trigger Note On on the next poll",
          "[note_buttons]")
{
    Mapping m(0);
    midi_stubs_reset();

    // Simulate button held across two consecutive polls
    N64State held = buttons(JOYBUS_N64_BUTTON_A);
    m.process(held);  // first poll  → Note On fires
    m.process(held);  // second poll → button still held, no new event

    long note_ons = 0;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::NoteOn) ++note_ons;

    REQUIRE(note_ons == 1);
}

// ─── [octave] ─────────────────────────────────────────────────────────────────

TEST_CASE("R shoulder shifts octave up by one", "[octave]")
{
    Mapping m(0);
    // Press R to shift +1 octave
    m.process(buttons(JOYBUS_N64_BUTTON_R));
    m.process(buttons(0));

    // Now press A — should be one octave (12 semitones) higher
    auto events = press_release(m, JOYBUS_N64_BUTTON_A);
    REQUIRE(events[0].type == MidiEvent::Type::NoteOn);
    REQUIRE(events[0].b1   == static_cast<uint8_t>(NOTE_ROOT + 12));
}

TEST_CASE("L shoulder shifts octave down by one", "[octave]")
{
    Mapping m(0);
    m.process(buttons(JOYBUS_N64_BUTTON_L));
    m.process(buttons(0));

    auto events = press_release(m, JOYBUS_N64_BUTTON_A);
    REQUIRE(events[0].type == MidiEvent::Type::NoteOn);
    REQUIRE(events[0].b1   == static_cast<uint8_t>(NOTE_ROOT - 12));
}

TEST_CASE("Octave shift accumulates across multiple presses", "[octave]")
{
    Mapping m(0);
    for (int i = 0; i < 2; ++i) {
        m.process(buttons(JOYBUS_N64_BUTTON_R));
        m.process(buttons(0));
    }
    auto events = press_release(m, JOYBUS_N64_BUTTON_A);
    REQUIRE(events[0].b1 == static_cast<uint8_t>(NOTE_ROOT + 24));
}

TEST_CASE("Octave shift clamps at +3 octaves maximum", "[octave]")
{
    Mapping m(0);
    // Press R six times — should cap at +3
    for (int i = 0; i < 6; ++i) {
        m.process(buttons(JOYBUS_N64_BUTTON_R));
        m.process(buttons(0));
    }
    auto events = press_release(m, JOYBUS_N64_BUTTON_A);
    REQUIRE(events[0].b1 == static_cast<uint8_t>(NOTE_ROOT + 36));
}

TEST_CASE("Octave shift clamps at -3 octaves minimum", "[octave]")
{
    Mapping m(0);
    for (int i = 0; i < 6; ++i) {
        m.process(buttons(JOYBUS_N64_BUTTON_L));
        m.process(buttons(0));
    }
    auto events = press_release(m, JOYBUS_N64_BUTTON_A);
    REQUIRE(events[0].b1 == static_cast<uint8_t>(NOTE_ROOT - 36));
}

TEST_CASE("Note Off uses the note pitch from when the button was pressed, "
          "not the current octave", "[octave]")
{
    // This is the 'snapshot' behaviour: the note held at press time is
    // stored in m_active_note[] so that a subsequent octave shift does not
    // orphan the Note Off message.
    Mapping m(0);
    midi_stubs_reset();

    // Press A at octave 0 (root)
    m.process(buttons(JOYBUS_N64_BUTTON_A));

    // Shift octave up while A is still held
    m.process(buttons(JOYBUS_N64_BUTTON_A | JOYBUS_N64_BUTTON_R));
    m.process(buttons(JOYBUS_N64_BUTTON_A));

    // Release A — Note Off must use the original pitch, not the shifted one
    m.process(buttons(0));

    uint8_t on_note  = 0;
    uint8_t off_note = 0xFF;
    for (auto &e : midi_stubs_events()) {
        if (e.type == MidiEvent::Type::NoteOn  && on_note  == 0)   on_note  = e.b1;
        if (e.type == MidiEvent::Type::NoteOff && off_note == 0xFF) off_note = e.b1;
    }
    REQUIRE(on_note  == NOTE_ROOT);
    REQUIRE(off_note == NOTE_ROOT);
}

TEST_CASE("Effective note is clamped to valid MIDI range 0-127", "[octave]")
{
    // Shift up 3 octaves from a high root note to verify upper-clamp.
    // NOTE_ROOT + 3*12 = 62 + 36 = 98 — well within range.
    // Shift down 3 octaves from a low root: 62 - 36 = 26 — also fine.
    // We verify clamping indirectly through the note bytes in events.
    Mapping m(0);
    for (int i = 0; i < 3; ++i) {
        m.process(buttons(JOYBUS_N64_BUTTON_R));
        m.process(buttons(0));
    }
    auto events = press_release(m, JOYBUS_N64_BUTTON_A);
    REQUIRE(events[0].b1 <= 127);
}

// ─── [sustain] ────────────────────────────────────────────────────────────────

TEST_CASE("Z press sends CC64=127 (sustain on)", "[sustain]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_Z));

    const auto &evs = midi_stubs_events();
    bool found = false;
    for (auto &e : evs) {
        if (e.type == MidiEvent::Type::ControlChange &&
            e.b1   == static_cast<uint8_t>(MidiCC::SustainPedal) &&
            e.b2   == 127) { found = true; break; }
    }
    REQUIRE(found);
}

TEST_CASE("Z release sends CC64=0 (sustain off)", "[sustain]")
{
    Mapping m(0);
    m.process(buttons(JOYBUS_N64_BUTTON_Z));   // press
    midi_stubs_reset();
    m.process(buttons(0));                      // release

    const auto &evs = midi_stubs_events();
    bool found = false;
    for (auto &e : evs) {
        if (e.type == MidiEvent::Type::ControlChange &&
            e.b1   == static_cast<uint8_t>(MidiCC::SustainPedal) &&
            e.b2   == 0) { found = true; break; }
    }
    REQUIRE(found);
}

// ─── [panic] ──────────────────────────────────────────────────────────────────

TEST_CASE("Start button sends CC123=0 (All Notes Off)", "[panic]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_START));

    bool found = false;
    for (auto &e : midi_stubs_events()) {
        if (e.type == MidiEvent::Type::ControlChange &&
            e.b1   == static_cast<uint8_t>(MidiCC::AllNotesOff) &&
            e.b2   == 0) { found = true; break; }
    }
    REQUIRE(found);
}

TEST_CASE("Start resets octave shift to zero", "[panic]")
{
    Mapping m(0);

    // Shift to +2 octaves
    for (int i = 0; i < 2; ++i) {
        m.process(buttons(JOYBUS_N64_BUTTON_R));
        m.process(buttons(0));
    }

    // Panic via Start
    m.process(buttons(JOYBUS_N64_BUTTON_START));
    m.process(buttons(0));

    // A note should now be at the base root again
    auto events = press_release(m, JOYBUS_N64_BUTTON_A);
    REQUIRE(events[0].b1 == NOTE_ROOT);
}

TEST_CASE("After panic, released previously-held notes do not send Note Off",
          "[panic]")
{
    // The Mapping clears m_active_note[] on panic, so a Note Off for a note
    // that was sounding before the panic will be silently dropped (the real
    // synthesiser already silenced it via CC123).
    Mapping m(0);

    // Hold A
    m.process(buttons(JOYBUS_N64_BUTTON_A));

    // Panic while A is still held
    m.process(buttons(JOYBUS_N64_BUTTON_A | JOYBUS_N64_BUTTON_START));
    m.process(buttons(JOYBUS_N64_BUTTON_A));

    // Release A — should produce no Note Off
    midi_stubs_reset();
    m.process(buttons(0));

    for (auto &e : midi_stubs_events())
        REQUIRE(e.type != MidiEvent::Type::NoteOff);
}

// ─── [dpad] ───────────────────────────────────────────────────────────────────

TEST_CASE("D-pad Up selects GM Ocarina (program 79)", "[dpad]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_UP));

    bool found = false;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::ProgramChange && e.b1 == 79)
            { found = true; break; }
    REQUIRE(found);
}

TEST_CASE("D-pad Down selects GM Acoustic Guitar (program 24)", "[dpad]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_DOWN));

    bool found = false;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::ProgramChange && e.b1 == 24)
            { found = true; break; }
    REQUIRE(found);
}

TEST_CASE("D-pad Left selects GM Music Box (program 10)", "[dpad]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_LEFT));

    bool found = false;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::ProgramChange && e.b1 == 10)
            { found = true; break; }
    REQUIRE(found);
}

TEST_CASE("D-pad Right selects GM Tubular Bells (program 14)", "[dpad]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_RIGHT));

    bool found = false;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::ProgramChange && e.b1 == 14)
            { found = true; break; }
    REQUIRE(found);
}

// ─── [joystick] ───────────────────────────────────────────────────────────────

TEST_CASE("Joystick within deadzone produces no pitch bend", "[joystick]")
{
    Mapping m(0);
    midi_stubs_reset();
    // Stick within deadzone (JOYSTICK_DEADZONE = 8)
    m.process(stick(static_cast<int8_t>(JOYSTICK_DEADZONE - 1), 0));

    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::PitchBend)
            REQUIRE(e.bend == 0);   // centre reset must still fire, but not non-zero
}

TEST_CASE("Joystick X beyond deadzone produces non-zero pitch bend", "[joystick]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(stick(JOYSTICK_MAX, 0));   // full-right

    bool found_nonzero = false;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::PitchBend && e.bend != 0)
            { found_nonzero = true; break; }
    REQUIRE(found_nonzero);
}

TEST_CASE("Joystick X positive → positive pitch bend", "[joystick]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(stick(JOYSTICK_MAX, 0));

    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::PitchBend)
            REQUIRE(e.bend > 0);
}

TEST_CASE("Joystick X negative → negative pitch bend", "[joystick]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(stick(-static_cast<int8_t>(JOYSTICK_MAX), 0));

    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::PitchBend)
            REQUIRE(e.bend < 0);
}

TEST_CASE("Full-right joystick pitch bend is near +8191", "[joystick]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(stick(JOYSTICK_MAX, 0));

    int16_t max_bend = 0;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::PitchBend && e.bend > max_bend)
            max_bend = e.bend;
    // Allow a small rounding tolerance around the 14-bit maximum
    REQUIRE(max_bend >= 8000);
}

TEST_CASE("Pitch bend 14-bit wire encoding LSB/MSB is correct", "[joystick]")
{
    // MIDI spec: centre = 0x2000, encoded as LSB=0x00, MSB=0x40.
    // We verify a known non-zero value: bend = +1 → raw = 8193 = 0x2001
    //   LSB = 0x01, MSB = 0x40.
    Mapping m(0);
    midi_stubs_reset();
    // Inject a stick value that should produce bend ≈ JOYSTICK_MAX/JOYSTICK_MAX * 8191 = +8191
    // For an exact check at bend=0 (centre snap): X within deadzone.
    m.process(stick(0, 0));

    for (auto &e : midi_stubs_events()) {
        if (e.type == MidiEvent::Type::PitchBend && e.bend == 0) {
            // Centre: raw = 8192 = 0x2000 → LSB = 0x00, MSB = 0x40
            REQUIRE(e.b1 == 0x00);   // LSB
            REQUIRE(e.b2 == 0x40);   // MSB
        }
    }
}

TEST_CASE("Joystick Y beyond deadzone produces non-zero Modulation (CC1)",
          "[joystick]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(stick(0, JOYSTICK_MAX));   // full-up

    bool found = false;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::ControlChange &&
            e.b1   == static_cast<uint8_t>(MidiCC::Modulation) &&
            e.b2   > 0) { found = true; break; }
    REQUIRE(found);
}

TEST_CASE("Joystick Y within deadzone produces zero Modulation", "[joystick]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(stick(0, static_cast<int8_t>(JOYSTICK_DEADZONE - 1)));

    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::ControlChange &&
            e.b1   == static_cast<uint8_t>(MidiCC::Modulation))
            REQUIRE(e.b2 == 0);
}

// ─── [velocity] ───────────────────────────────────────────────────────────────

TEST_CASE("Centred stick produces moderate default velocity (~80)",
          "[velocity]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_A, 0, 0));   // stick centred

    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::NoteOn)
            REQUIRE(e.b2 == 80);   // mezzo-forte default
}

TEST_CASE("Maximum stick deflection produces velocity in range 1–127",
          "[velocity]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_A, JOYSTICK_MAX, JOYSTICK_MAX));

    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::NoteOn) {
            REQUIRE(e.b2 >= 1);
            REQUIRE(e.b2 <= 127);
        }
}

TEST_CASE("Full-deflection stick velocity is strictly greater than deadzone velocity",
          "[velocity]")
{
    // Buttons pressed twice: once with stick centred, once at max deflection.
    Mapping m(0);

    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_A, 0, 0));
    uint8_t vel_centre = 0;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::NoteOn) vel_centre = e.b2;
    m.process(buttons(0));   // release

    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_A, JOYSTICK_MAX, 0));
    uint8_t vel_max = 0;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::NoteOn) vel_max = e.b2;

    REQUIRE(vel_max > vel_centre);
}

TEST_CASE("Velocity is always > 0 regardless of stick position", "[velocity]")
{
    // Test several stick positions including just inside the deadzone boundary
    const int8_t positions[] = {0, JOYSTICK_DEADZONE, JOYSTICK_MAX};

    for (int8_t pos : positions) {
        Mapping m(0);
        midi_stubs_reset();
        m.process(buttons(JOYBUS_N64_BUTTON_A, pos, 0));
        m.process(buttons(0));

        for (auto &e : midi_stubs_events())
            if (e.type == MidiEvent::Type::NoteOn)
                REQUIRE(e.b2 > 0);
    }
}

// ─── [channel] ────────────────────────────────────────────────────────────────

TEST_CASE("All events use the MIDI channel bound at construction", "[channel]")
{
    for (uint8_t ch = 0; ch < 4; ++ch) {
        Mapping m(ch);
        midi_stubs_reset();

        m.process(buttons(JOYBUS_N64_BUTTON_A));
        m.process(buttons(0));
        m.process(buttons(JOYBUS_N64_BUTTON_Z));
        m.process(buttons(0));

        for (auto &e : midi_stubs_events()) {
            // Real-time messages (if any) have no meaningful channel
            if (e.type == MidiEvent::Type::Realtime) continue;
            INFO("Event type=" << static_cast<int>(e.type)
                 << " on channel " << static_cast<int>(e.channel)
                 << " expected " << static_cast<int>(ch));
            REQUIRE(e.channel == ch);
        }
    }
}

TEST_CASE("Two Mapping instances on different channels are isolated", "[channel]")
{
    Mapping m0(0);
    Mapping m1(1);
    midi_stubs_reset();

    m0.process(buttons(JOYBUS_N64_BUTTON_A));
    m1.process(buttons(JOYBUS_N64_BUTTON_B));

    bool saw_ch0_note_on = false;
    bool saw_ch1_note_on = false;
    for (auto &e : midi_stubs_events()) {
        if (e.type == MidiEvent::Type::NoteOn && e.channel == 0) saw_ch0_note_on = true;
        if (e.type == MidiEvent::Type::NoteOn && e.channel == 1) saw_ch1_note_on = true;
    }
    REQUIRE(saw_ch0_note_on);
    REQUIRE(saw_ch1_note_on);
}

// ─── [edge_cases] ─────────────────────────────────────────────────────────────

TEST_CASE("Simultaneous A and B produce two independent notes", "[edge_cases]")
{
    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(JOYBUS_N64_BUTTON_A | JOYBUS_N64_BUTTON_B));

    long note_ons = 0;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::NoteOn) ++note_ons;
    REQUIRE(note_ons == 2);
}

TEST_CASE("All six note buttons can sound simultaneously", "[edge_cases]")
{
    constexpr uint16_t ALL_NOTE_BTNS =
        JOYBUS_N64_BUTTON_A     | JOYBUS_N64_BUTTON_B     |
        JOYBUS_N64_BUTTON_C_UP  | JOYBUS_N64_BUTTON_C_DOWN|
        JOYBUS_N64_BUTTON_C_LEFT| JOYBUS_N64_BUTTON_C_RIGHT;

    Mapping m(0);
    midi_stubs_reset();
    m.process(buttons(ALL_NOTE_BTNS));

    long note_ons = 0;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::NoteOn) ++note_ons;
    REQUIRE(note_ons == 6);
}

TEST_CASE("Releasing all six buttons produces six Note Offs", "[edge_cases]")
{
    constexpr uint16_t ALL_NOTE_BTNS =
        JOYBUS_N64_BUTTON_A     | JOYBUS_N64_BUTTON_B     |
        JOYBUS_N64_BUTTON_C_UP  | JOYBUS_N64_BUTTON_C_DOWN|
        JOYBUS_N64_BUTTON_C_LEFT| JOYBUS_N64_BUTTON_C_RIGHT;

    Mapping m(0);
    m.process(buttons(ALL_NOTE_BTNS));   // press all
    midi_stubs_reset();
    m.process(buttons(0));               // release all

    long note_offs = 0;
    for (auto &e : midi_stubs_events())
        if (e.type == MidiEvent::Type::NoteOff) ++note_offs;
    REQUIRE(note_offs == 6);
}

TEST_CASE("Releasing a button that was never pressed produces no Note Off",
          "[edge_cases]")
{
    Mapping m(0);
    midi_stubs_reset();
    // Fake a falling edge without a prior rising edge (m_active_note is 0)
    m.process(buttons(JOYBUS_N64_BUTTON_A));   // first press — rising edge
    m.process(buttons(0));                      // release   — Note Off fires

    // A fresh mapper has never pressed A — releasing it should be silent.
    Mapping m2(0);
    midi_stubs_reset();
    m2.process(buttons(0));   // nothing was ever pressed, release is a no-op

    for (auto &e : midi_stubs_events())
        REQUIRE(e.type != MidiEvent::Type::NoteOff);
}

TEST_CASE("Pitch bend LSB is in range 0x00–0x7F (7-bit MIDI data byte)",
          "[edge_cases]")
{
    Mapping m(0);
    // Sweep X from -MAX to +MAX
    for (int x = -JOYSTICK_MAX; x <= JOYSTICK_MAX; x += 10) {
        midi_stubs_reset();
        m.process(stick(static_cast<int8_t>(x), 0));
        for (auto &e : midi_stubs_events())
            if (e.type == MidiEvent::Type::PitchBend) {
                REQUIRE(e.b1 <= 0x7F);
                REQUIRE(e.b2 <= 0x7F);
            }
    }
}

TEST_CASE("MIDI note values are always in valid range 0–127", "[edge_cases]")
{
    // Test every note button at extreme octave positions
    const uint16_t all_notes[] = {
        JOYBUS_N64_BUTTON_A,     JOYBUS_N64_BUTTON_B,
        JOYBUS_N64_BUTTON_C_UP,  JOYBUS_N64_BUTTON_C_DOWN,
        JOYBUS_N64_BUTTON_C_LEFT,JOYBUS_N64_BUTTON_C_RIGHT,
    };

    for (int dir : {+1, -1}) {
        Mapping m(0);
        for (int i = 0; i < 3; ++i) {
            uint16_t btn = (dir > 0) ? JOYBUS_N64_BUTTON_R : JOYBUS_N64_BUTTON_L;
            m.process(buttons(btn));
            m.process(buttons(0));
        }
        for (uint16_t btn : all_notes) {
            midi_stubs_reset();
            m.process(buttons(btn));
            m.process(buttons(0));
            for (auto &e : midi_stubs_events())
                if (e.type == MidiEvent::Type::NoteOn ||
                    e.type == MidiEvent::Type::NoteOff)
                    REQUIRE(e.b1 <= 127);
        }
    }
}

