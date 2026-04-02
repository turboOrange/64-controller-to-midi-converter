/**
 * @file main.cpp
 * @brief Entry point for the 64-Controller-to-MIDI Converter firmware.
 *
 * Initialises all hardware peripherals and launches one FreeRTOS task per
 * N64 controller plus an optional MIDI real-time clock task, then hands
 * control to the SMP FreeRTOS scheduler.
 *
 * All four controller tasks run concurrently across both RP2040 Cortex-M0+
 * cores.  A shared mutex inside midi.cpp ensures MIDI byte sequences are
 * never interleaved between tasks — like four musicians playing the same
 * ocarina, each in perfect time.
 *
 * Hardware resources initialised here (before the scheduler starts):
 *   • GPIO LED
 *   • All four Joybus PIO state machines (sequential, single-core, safe)
 *   • MIDI UART1 + FreeRTOS write mutex
 */

#include "config.h"
#include "n64_controller.h"
#include "midi.h"
#include "controller_task.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"

// ─── Static task memory ───────────────────────────────────────────────────────
// Using xTaskCreateStatic avoids any heap allocation for the task control
// blocks and stacks — following the project's "no dynamic allocation" rule.

static ControllerContext s_ctrl_ctx[NUM_CONTROLLERS];
static StackType_t       s_ctrl_stacks[NUM_CONTROLLERS][CONTROLLER_TASK_STACK_WORDS];
static StaticTask_t      s_ctrl_tcbs  [NUM_CONTROLLERS];

#if MIDI_CLOCK_ENABLED
static StackType_t  s_clock_stack[MIDI_CLOCK_TASK_STACK_WORDS];
static StaticTask_t s_clock_tcb;
#endif

// ─── FreeRTOS fault hooks ─────────────────────────────────────────────────────

extern "C" {

/// Called when the FreeRTOS heap is exhausted (configUSE_MALLOC_FAILED_HOOK=1).
void vApplicationMallocFailedHook(void)
{
    // Rapid LED blink — heap exhausted, firmware cannot continue.
    while (true) {
        gpio_put(PIN_LED, 1); sleep_ms(50);
        gpio_put(PIN_LED, 0); sleep_ms(50);
    }
}

/// Called when a task overflows its stack (configCHECK_FOR_STACK_OVERFLOW=2).
void vApplicationStackOverflowHook(TaskHandle_t /*task*/, char * /*name*/)
{
    // Slow LED blink — distinct pattern from malloc failure.
    while (true) {
        gpio_put(PIN_LED, 1); sleep_ms(200);
        gpio_put(PIN_LED, 0); sleep_ms(200);
    }
}

}  // extern "C"

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    stdio_init_all();

    // LED — heartbeat driven by controller 0 task after scheduler starts.
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0);

    // ── Initialise all Joybus interfaces (sequential, before scheduler) ───
    // All four calls happen on Core 0 before any task runs, preventing the
    // non-thread-safe PIO program load in libjoybus from racing.
    for (uint8_t i = 0; i < NUM_CONTROLLERS; ++i) {
        if (!n64_controller_init(i)) {
            // Halt with a distinct triple-blink error pattern.
            while (true) {
                for (int b = 0; b < 3; ++b) {
                    gpio_put(PIN_LED, 1); sleep_ms(100);
                    gpio_put(PIN_LED, 0); sleep_ms(100);
                }
                sleep_ms(500);
            }
        }
    }

    // ── Bring up MIDI UART + mutex + initial program changes ─────────────
    midi_init();

    // ── Create one controller task per N64 port ───────────────────────────
    for (uint8_t i = 0; i < NUM_CONTROLLERS; ++i) {
        s_ctrl_ctx[i].idx     = i;
        s_ctrl_ctx[i].channel = CONTROLLER_CHANNELS[i];

        char name[12];
        // "ctrl0" … "ctrl3"
        name[0] = 'c'; name[1] = 't'; name[2] = 'r'; name[3] = 'l';
        name[4] = static_cast<char>('0' + i); name[5] = '\0';

        xTaskCreateStatic(
            controller_task_fn,
            name,
            CONTROLLER_TASK_STACK_WORDS,
            &s_ctrl_ctx[i],
            CONTROLLER_TASK_PRIORITY,
            s_ctrl_stacks[i],
            &s_ctrl_tcbs[i]
        );
    }

    // ── Optional MIDI real-time clock task ────────────────────────────────
#if MIDI_CLOCK_ENABLED
    xTaskCreateStatic(
        midi_clock_task_fn,
        "midi_clk",
        MIDI_CLOCK_TASK_STACK_WORDS,
        nullptr,
        MIDI_CLOCK_TASK_PRIORITY,
        s_clock_stack,
        &s_clock_tcb
    );
#endif

    // Hand control to the FreeRTOS SMP scheduler — never returns.
    vTaskStartScheduler();

    // Unreachable; satisfies the compiler.
    return 0;
}
