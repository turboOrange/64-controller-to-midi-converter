/**
 * @file n64_controller_sim.cpp
 * @brief Simulation stub for n64_controller.cpp — used when SIMULATION=1.
 *
 * Replaces the PIO/DMA/FreeRTOS Joybus driver with a shared-memory
 * interface.  An external tool (the Renode tester, Robot Framework, etc.)
 * writes N64 controller state into a known SRAM region; every call to
 * n64_controller_poll() reads that region and returns it directly.
 *
 * Memory layout at SIM_STATE_BASE — one 4-byte SimSlot per controller,
 * laid out so a single 32-bit little-endian write covers the whole slot:
 *
 *   offset 0-1 : buttons  (uint16_t, JOYBUS_N64_BUTTON_* bitmask)
 *   offset 2   : stick_x  (int8_t,  −128…+127, positive = right)
 *   offset 3   : stick_y  (int8_t,  −128…+127, positive = up)
 *
 * To inject a button press from the Renode monitor:
 *   sysbus WriteDoubleWord 0x20040000 0x00000080   ← A button, stick centred
 *
 * Value packing (Python):
 *   value = (buttons & 0xFFFF) | ((stick_x & 0xFF) << 16) | ((stick_y & 0xFF) << 24)
 */

#include "n64_controller.h"
#include "config.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

// ─── Shared-memory layout ─────────────────────────────────────────────────────

/**
 * Base SRAM address for the simulated controller state.
 * Placed at the top of the RP2040's 264 KB SRAM (0x20000000 + 0x40000),
 * well above the firmware's normal heap/stack region.
 */
static constexpr uint32_t SIM_STATE_BASE = 0x20040000u;

/** One slot per controller — exactly 4 bytes, same layout as a 32-bit word. */
struct SimSlot {
    uint16_t buttons;  ///< Pressed-button bitmask (JOYBUS_N64_BUTTON_*)
    int8_t   stick_x;  ///< Joystick X, signed −128…+127
    int8_t   stick_y;  ///< Joystick Y, signed −128…+127
} __attribute__((packed));

static_assert(sizeof(SimSlot) == 4, "SimSlot must be exactly 4 bytes");

/**
 * Volatile pointer into SRAM so the compiler never optimises away reads
 * of values that were written by an external agent (Renode).
 */
static volatile SimSlot * const s_slots =
    reinterpret_cast<volatile SimSlot *>(SIM_STATE_BASE);

// ─── Public API ───────────────────────────────────────────────────────────────

bool n64_controller_init(uint8_t /*idx*/)
{
    // Zero the shared region once on first call so all slots start in a
    // known state (no buttons pressed, stick centred).
    static bool done = false;
    if (!done) {
        memset(reinterpret_cast<void *>(SIM_STATE_BASE),
               0, sizeof(SimSlot) * NUM_CONTROLLERS);
        done = true;
    }
    return true;
}

bool n64_controller_poll(uint8_t idx, N64State &state)
{
    if (idx >= NUM_CONTROLLERS) return false;

    // Yield one FreeRTOS tick so vTaskDelayUntil(100 Hz) in controller_task_fn
    // still paces itself and the scheduler can service other tasks.
    vTaskDelay(1);

    const volatile SimSlot &slot = s_slots[idx];
    state.buttons = slot.buttons;
    state.stick_x = slot.stick_x;
    state.stick_y = slot.stick_y;
    return true;
}

