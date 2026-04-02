#include "mapping.h"
#include "midi.h"
#include "config.h"

extern "C" {
#include <joybus/n64.h>
}

#include <cstdlib>   // abs()
#include <algorithm> // std::clamp

// ─── Button-to-array-index mapping ───────────────────────────────────────────
// Keep in sync with NUM_NOTE_BUTTONS in mapping.h.
enum BtnIdx : uint8_t {
    BTN_A      = 0,
    BTN_B      = 1,
    BTN_C_UP   = 2,
    BTN_C_DOWN = 3,
    BTN_C_LEFT = 4,
    BTN_C_RIGHT= 5,
};

// Parallel array: bitmask for each note button in the order above.
static constexpr uint16_t k_note_button_mask[Mapping::NUM_NOTE_BUTTONS] = {
    JOYBUS_N64_BUTTON_A,
    JOYBUS_N64_BUTTON_B,
    JOYBUS_N64_BUTTON_C_UP,
    JOYBUS_N64_BUTTON_C_DOWN,
    JOYBUS_N64_BUTTON_C_LEFT,
    JOYBUS_N64_BUTTON_C_RIGHT,
};

// Base note for each button, pulled from config.h constants.
static constexpr uint8_t k_base_note[Mapping::NUM_NOTE_BUTTONS] = {
    NOTE_ROOT,
    NOTE_B,
    NOTE_C_UP,
    NOTE_C_DOWN,
    NOTE_C_LEFT,
    NOTE_C_RIGHT,
};

// ─── Helpers ──────────────────────────────────────────────────────────────────

uint8_t Mapping::effective_note(uint8_t base_note) const
{
    int16_t note = static_cast<int16_t>(base_note) + (m_octave_shift * 12);
    return static_cast<uint8_t>(std::clamp<int16_t>(note, 0, 127));
}

uint8_t Mapping::joystick_velocity(int8_t x, int8_t y) const
{
    // Euclidean distance clamped to JOYSTICK_MAX, then scaled to 1–127.
    float dx = static_cast<float>(x);
    float dy = static_cast<float>(y);
    float mag = dx * dx + dy * dy;

    // Integer square-root approximation (fast, no libm dependency required).
    uint32_t imag = static_cast<uint32_t>(mag);
    uint32_t root = 0;
    uint32_t bit  = 1u << 30;
    while (bit > imag)       bit >>= 2;
    while (bit != 0) {
        if (imag >= root + bit) { imag -= root + bit; root = (root >> 1) + bit; }
        else                    { root >>= 1; }
        bit >>= 2;
    }
    uint8_t dist = static_cast<uint8_t>(std::min<uint32_t>(root, JOYSTICK_MAX));

    if (dist < JOYSTICK_DEADZONE) {
        // Stick is centred — use a moderate default velocity (mezzo-forte).
        return 80;
    }

    // Map JOYSTICK_DEADZONE…JOYSTICK_MAX → 1…127.
    uint8_t scaled = static_cast<uint8_t>(
        1 + (static_cast<uint16_t>(dist - JOYSTICK_DEADZONE) * 126u)
              / (JOYSTICK_MAX - JOYSTICK_DEADZONE));
    return std::clamp<uint8_t>(scaled, 1, 127);
}

int16_t Mapping::axis_to_pitch_bend(int8_t axis) const
{
    // Map –JOYSTICK_MAX…+JOYSTICK_MAX → –8192…+8191.
    int32_t bend = (static_cast<int32_t>(axis) * 8192) / JOYSTICK_MAX;
    return static_cast<int16_t>(std::clamp<int32_t>(bend, -8192, 8191));
}

void Mapping::on_button_pressed(uint8_t btn_index, uint8_t base_note, const N64State &state)
{
    uint8_t note = effective_note(base_note);
    uint8_t vel  = joystick_velocity(state.stick_x, state.stick_y);
    m_active_note[btn_index] = note;
    midi_note_on(MIDI_CHANNEL, note, vel);
}

void Mapping::on_button_released(uint8_t btn_index)
{
    uint8_t note = m_active_note[btn_index];
    if (note != 0) {
        midi_note_off(MIDI_CHANNEL, note);
        m_active_note[btn_index] = 0;
    }
}

// ─── Main process() ───────────────────────────────────────────────────────────

void Mapping::process(const N64State &state)
{
    const uint16_t prev    = m_prev.buttons;
    const uint16_t current = state.buttons;
    const uint16_t pressed = (~prev) & current;   // Rising edges.
    const uint16_t released = prev & (~current);  // Falling edges.

    // ── Note buttons (A, B, C-Up/Down/Left/Right) ─────────────────────────
    for (uint8_t i = 0; i < NUM_NOTE_BUTTONS; ++i) {
        if (pressed  & k_note_button_mask[i]) on_button_pressed (i, k_base_note[i], state);
        if (released & k_note_button_mask[i]) on_button_released(i);
    }

    // ── Z button → Sustain pedal (CC64) ───────────────────────────────────
    if (pressed  & JOYBUS_N64_BUTTON_Z) {
        m_sustain_active = true;
        midi_control_change(MIDI_CHANNEL, static_cast<uint8_t>(MidiCC::SustainPedal), 127);
    }
    if (released & JOYBUS_N64_BUTTON_Z) {
        m_sustain_active = false;
        midi_control_change(MIDI_CHANNEL, static_cast<uint8_t>(MidiCC::SustainPedal), 0);
    }

    // ── L / R shoulders → Octave shift ────────────────────────────────────
    if (pressed & JOYBUS_N64_BUTTON_L) {
        m_octave_shift = std::max<int8_t>(m_octave_shift - 1, -3);
    }
    if (pressed & JOYBUS_N64_BUTTON_R) {
        m_octave_shift = std::min<int8_t>(m_octave_shift + 1, +3);
    }

    // ── Start → MIDI Panic ────────────────────────────────────────────────
    if (pressed & JOYBUS_N64_BUTTON_START) {
        midi_panic(MIDI_CHANNEL);
        // Clear all tracked active notes.
        for (auto &n : m_active_note) n = 0;
        m_sustain_active = false;
        m_octave_shift   = 0;
    }

    // ── D-pad → Program Change (instrument select) ────────────────────────
    if (pressed & JOYBUS_N64_BUTTON_UP)   midi_program_change(MIDI_CHANNEL, 79);  // Ocarina
    if (pressed & JOYBUS_N64_BUTTON_DOWN) midi_program_change(MIDI_CHANNEL, 24);  // Acoustic Guitar
    if (pressed & JOYBUS_N64_BUTTON_LEFT) midi_program_change(MIDI_CHANNEL, 10);  // Music Box
    if (pressed & JOYBUS_N64_BUTTON_RIGHT)midi_program_change(MIDI_CHANNEL, 14);  // Tubular Bells

    // ── Joystick → Pitch Bend (X-axis) + Modulation (Y-axis) ─────────────
    {
        int8_t x = state.stick_x;
        int8_t y = state.stick_y;

        if (abs(x) > JOYSTICK_DEADZONE) {
            midi_pitch_bend(MIDI_CHANNEL, axis_to_pitch_bend(x));
        } else {
            midi_pitch_bend(MIDI_CHANNEL, 0);  // Snap back to centre.
        }

        uint8_t mod = (abs(y) > JOYSTICK_DEADZONE)
            ? static_cast<uint8_t>((static_cast<uint16_t>(abs(y)) * 127u) / JOYSTICK_MAX)
            : 0;
        midi_control_change(MIDI_CHANNEL, static_cast<uint8_t>(MidiCC::Modulation), mod);
    }

    m_prev = state;
}

