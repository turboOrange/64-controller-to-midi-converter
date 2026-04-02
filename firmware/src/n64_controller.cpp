#include "n64_controller.h"
#include "config.h"

extern "C" {
#include <joybus/joybus.h>
#include <joybus/backend/rp2xxx.h>
#include <joybus/host/n64.h>
#include <joybus/n64.h>
}

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "FreeRTOS.h"
#include "task.h"

// ─── Per-instance state ───────────────────────────────────────────────────────

/**
 * @brief All state owned by a single Joybus controller instance.
 *
 * Statically allocated — one slot per controller, zero heap involvement.
 */
struct ControllerInst {
    struct joybus_rp2xxx rp2xxx_bus;             ///< libjoybus RP2xxx backend struct.
    struct joybus       *bus;                    ///< Generic bus pointer (= JOYBUS(&rp2xxx_bus)).
    uint8_t              response[JOYBUS_BLOCK_SIZE]; ///< Raw DMA receive buffer.
    volatile int         transfer_result;        ///< Set by ISR callback; 0 or error code.
    TaskHandle_t         task_handle;            ///< Calling task, updated each poll().
};

static ControllerInst s_inst[NUM_CONTROLLERS];

// ─── ISR callback ─────────────────────────────────────────────────────────────

/**
 * @brief Called from the PIO/alarm ISR when a Joybus transfer completes.
 *
 * Stores the result and wakes the blocked controller task via a FreeRTOS
 * task notification — no spin-waiting required.
 */
static void transfer_cb(struct joybus *bus, int result, void *user_data)
{
    (void)bus;
    auto *inst = static_cast<ControllerInst *>(user_data);
    inst->transfer_result = result;

    BaseType_t higher_prio_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(inst->task_handle, &higher_prio_task_woken);
    portYIELD_FROM_ISR(higher_prio_task_woken);
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool n64_controller_init(uint8_t idx)
{
    if (idx >= NUM_CONTROLLERS) {
        return false;
    }

    ControllerInst &inst = s_inst[idx];
    inst.bus = JOYBUS(&inst.rp2xxx_bus);

    // Assign the GPIO pin from config and share pio0 across all instances.
    int rc = joybus_rp2xxx_init(&inst.rp2xxx_bus, CONTROLLER_PINS[idx], JOYBUS_PIO_INSTANCE);
    if (rc < 0) {
        return false;
    }

    joybus_enable(inst.bus);
    return true;
}

bool n64_controller_poll(uint8_t idx, N64State &state)
{
    if (idx >= NUM_CONTROLLERS) {
        return false;
    }

    ControllerInst &inst = s_inst[idx];

    // Record the calling task so the ISR knows which task to notify.
    inst.task_handle     = xTaskGetCurrentTaskHandle();
    inst.transfer_result = 0;

    // Drain any stale notification that might be pending from a previous cycle.
    ulTaskNotifyTake(pdTRUE, 0);

    int rc = joybus_n64_read(inst.bus, inst.response, transfer_cb, &inst);
    if (rc < 0) {
        return false;
    }

    // Block this task until the ISR signals completion (5 ms safety timeout).
    // Unlike the old spin-wait, this yields the CPU to other runnable tasks.
    const uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
    if (notified == 0) {
        return false;  // Timeout — controller likely disconnected.
    }

    if (inst.transfer_result < 0) {
        return false;
    }

    // Parse the 4-byte N64 response: [buttons_hi, buttons_lo, stick_x, stick_y].
    const auto *raw = reinterpret_cast<const joybus_n64_controller_input *>(inst.response);
    state.buttons = raw->buttons;
    state.stick_x = static_cast<int8_t>(raw->stick_x);
    state.stick_y = static_cast<int8_t>(raw->stick_y);

    return true;
}
