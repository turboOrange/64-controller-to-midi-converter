#pragma once

#include "n64_controller.h"
#include <stdint.h>

/**
 * @brief Stateful mapper that translates N64 controller input into MIDI events.
 *
 * Maintains previous button state to generate accurate Note On / Note Off edges
 * and tracks the current octave offset applied by the L / R shoulder buttons.
 */
class Mapping {
public:
    Mapping() = default;

    /// Number of note-producing buttons (A, B, C-Up/Down/Left/Right).
    static constexpr uint8_t NUM_NOTE_BUTTONS = 6;

    /**
     * @brief Process a fresh N64State and emit any required MIDI messages.
     *
     * Must be called once per poll cycle. Internally diffs against the
     * previous state so every button press/release produces exactly one
     * Note On / Note Off event.
     *
     * @param state Latest controller state from n64_controller_poll().
     */
    void process(const N64State &state);

private:
    // ── Previous-cycle snapshot for edge detection ────────────────────────
    N64State m_prev{};

    // ── Octave shift applied by L / R shoulder buttons (–3…+3) ───────────
    int8_t m_octave_shift{0};

    // ── Active note tracking (one slot per mappable button) ───────────────
    // Indexed by a small enum so we can send Note Off for the exact pitch
    // that was active when the button was pressed (even if octave changed).
    uint8_t m_active_note[NUM_NOTE_BUTTONS]{};  ///< 0 = no note active.

    // ── Z-button (sustain pedal) state ────────────────────────────────────
    bool m_sustain_active{false};

    // ── Helpers ───────────────────────────────────────────────────────────

    /**
     * @brief Compute the effective MIDI note for a given base pitch and the
     *        current octave shift, clamped to the valid MIDI range 0–127.
     */
    uint8_t effective_note(uint8_t base_note) const;

    /**
     * @brief Map joystick distance from centre to a MIDI velocity (1–127).
     *
     * Uses the magnitude of the stick vector so players can shape dynamics
     * the same way Link breathes into the ocarina.
     */
    uint8_t joystick_velocity(int8_t x, int8_t y) const;

    /**
     * @brief Convert a signed stick axis value to a 14-bit MIDI pitch-bend
     *        value (–8192…+8191).
     */
    int16_t axis_to_pitch_bend(int8_t axis) const;

    /**
     * @brief Handle a button that just went from released → pressed.
     * @param btn_index  Index into m_active_note[].
     * @param base_note  Root note for this button from config.h.
     * @param state      Current full controller state (for velocity).
     */
    void on_button_pressed(uint8_t btn_index, uint8_t base_note, const N64State &state);

    /**
     * @brief Handle a button that just went from pressed → released.
     * @param btn_index  Index into m_active_note[].
     */
    void on_button_released(uint8_t btn_index);
};

