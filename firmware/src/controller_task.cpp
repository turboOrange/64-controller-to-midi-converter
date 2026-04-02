/**
 * @file controller_task.cpp
 * @brief Per-controller FreeRTOS task implementation.
 *
 * Each task owns one Mapping instance (stack-allocated inside the task),
 * polls its dedicated N64 controller at 100 Hz, and routes MIDI output to
 * its assigned channel through the mutex-protected midi_* API.
 */

#include "controller_task.h"
#include "config.h"
#include "n64_controller.h"
#include "midi.h"
#include "mapping.h"

#include "FreeRTOS.h"
#include "task.h"
#include "hardware/gpio.h"

// ─── Controller task ──────────────────────────────────────────────────────────

void controller_task_fn(void *arg)
{
    const auto *ctx = static_cast<const ControllerContext *>(arg);

    // Each task owns its own stateful mapper — no shared mutable state.
    Mapping mapper(ctx->channel);

    // Anchor the first wakeup time to now so vTaskDelayUntil() gives a
    // steady 100 Hz cadence from task creation onwards.
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        // ── Precise 100 Hz poll timing ────────────────────────────────────
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(POLL_INTERVAL_MS));

        // ── Poll controller (blocks on task notification from PIO ISR) ────
        N64State state{};
        const bool ok = n64_controller_poll(ctx->idx, state);

        // Controller 0 drives the onboard LED as a system-wide heartbeat.
        if (ctx->idx == 0) {
            gpio_xor_mask(1u << PIN_LED);
        }

        if (!ok) {
            // Joybus error or timeout — silence this channel and keep polling.
            midi_panic(ctx->channel);
            continue;
        }

        // ── OoT-inspired MIDI mapping ─────────────────────────────────────
        mapper.process(state);
    }
}

// ─── MIDI clock task (optional) ───────────────────────────────────────────────

#if MIDI_CLOCK_ENABLED

/// Number of MIDI clock ticks per quarter note (MIDI 1.0 spec: 24).
static constexpr uint32_t MIDI_TICKS_PER_BEAT = 24;

/// Tick interval in ms at the configured BPM.
/// Example: 120 BPM → 60 000 / 120 / 24 ≈ 20 ms per clock tick.
static constexpr uint32_t MIDI_TICK_INTERVAL_MS =
    (60000u / MIDI_CLOCK_BPM) / MIDI_TICKS_PER_BEAT;

void midi_clock_task_fn(void *arg)
{
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MIDI_TICK_INTERVAL_MS));
        midi_realtime(MidiRealTime::Clock);
    }
}

#endif  // MIDI_CLOCK_ENABLED

