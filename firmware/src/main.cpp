/**
 * @file main.cpp
 * @brief Entry point for the 64-Controller-to-MIDI Converter firmware.
 *
 * Initialises the Joybus (N64 controller) and MIDI UART interfaces, then
 * enters a deterministic 100 Hz super-loop that:
 *   1. Polls the N64 controller via libjoybus PIO driver.
 *   2. Runs the OoT-inspired MIDI mapping.
 *   3. Optionally ticks the MIDI real-time clock.
 *
 * Like the notes of Saria's Song, the loop should be steady and unhurried —
 * every cycle exactly POLL_INTERVAL_MS milliseconds apart.
 */

#include "config.h"
#include "n64_controller.h"
#include "midi.h"
#include "mapping.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

// ─── MIDI clock helpers ───────────────────────────────────────────────────────

#if MIDI_CLOCK_ENABLED
/// Number of MIDI clock ticks per quarter note (MIDI 1.0 spec: 24).
static constexpr uint32_t MIDI_TICKS_PER_BEAT = 24;

/// Microseconds between MIDI clock ticks at the configured BPM.
static constexpr uint32_t MIDI_TICK_INTERVAL_US =
    (60'000'000u / MIDI_CLOCK_BPM) / MIDI_TICKS_PER_BEAT;

static uint32_t s_last_clock_us = 0;
#endif

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    // Pico SDK initialisation — sets up clocks and USB/UART stdio.
    stdio_init_all();

    // Onboard LED as a heartbeat indicator.
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);

    // Bring up the N64 Joybus interface (PIO state machine).
    if (!n64_controller_init()) {
        // If PIO resources are unavailable, pulse the LED rapidly and halt.
        while (true) {
            gpio_put(PIN_LED, 1); sleep_ms(50);
            gpio_put(PIN_LED, 0); sleep_ms(50);
        }
    }

    // Bring up MIDI UART and send the initial Program Change (Ocarina).
    midi_init();

    // Stateful OoT note mapper.
    Mapping mapper;

    // Absolute timestamp of the last controller poll (in ms).
    absolute_time_t next_poll = make_timeout_time_ms(POLL_INTERVAL_MS);

    while (true) {
        // ── MIDI real-time clock tick ─────────────────────────────────────
#if MIDI_CLOCK_ENABLED
        {
            uint32_t now_us = time_us_32();
            if (now_us - s_last_clock_us >= MIDI_TICK_INTERVAL_US) {
                midi_realtime(MidiRealTime::Clock);
                s_last_clock_us = now_us;
            }
        }
#endif

        // ── Wait for the next poll slot ───────────────────────────────────
        sleep_until(next_poll);
        next_poll = delayed_by_ms(next_poll, POLL_INTERVAL_MS);

        // ── Poll the controller ───────────────────────────────────────────
        N64State state{};
        bool ok = n64_controller_poll(state);

        // Toggle LED on each successful poll as a heartbeat.
        gpio_xor_mask(1u << PIN_LED);

        if (!ok) {
            // Controller disconnected or Joybus error — send panic and wait.
            midi_panic(MIDI_CHANNEL);
            continue;
        }

        // ── Run the MIDI mapping ──────────────────────────────────────────
        mapper.process(state);
    }

    return 0;
}

