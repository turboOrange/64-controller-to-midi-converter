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

// ─── Internal libjoybus state ─────────────────────────────────────────────────

static struct joybus_rp2xxx  s_rp2xxx_bus;
static struct joybus        *s_bus = JOYBUS(&s_rp2xxx_bus);

/// Raw response buffer — sized for JOYBUS_BLOCK_SIZE as required by libjoybus.
static uint8_t s_response[JOYBUS_BLOCK_SIZE];

// ─── Callback plumbing ────────────────────────────────────────────────────────

/// Indicates whether the most recent async transfer has completed.
static volatile bool s_transfer_done  = false;

/// 0 on success, negative libjoybus error code on failure.
static volatile int  s_transfer_result = 0;

static void transfer_cb(struct joybus *bus, int result, void *user_data)
{
    (void)bus;
    (void)user_data;
    s_transfer_result = result;
    s_transfer_done   = true;
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool n64_controller_init()
{
    int rc = joybus_rp2xxx_init(&s_rp2xxx_bus, PIN_JOYBUS, pio0);
    if (rc < 0) {
        return false;
    }

    joybus_enable(s_bus);
    return true;
}

bool n64_controller_poll(N64State &state)
{
    s_transfer_done   = false;
    s_transfer_result = 0;

    int rc = joybus_n64_read(s_bus, s_response, transfer_cb, nullptr);
    if (rc < 0) {
        return false;
    }

    // Spin-wait for the async callback — the Joybus transfer takes ~200 µs at
    // 1 Mbps, so this tight loop is acceptable at our 100 Hz poll rate.
    uint32_t deadline_us = time_us_32() + 5000u;  // 5 ms safety timeout
    while (!s_transfer_done) {
        if (time_us_32() > deadline_us) {
            return false;  // Timed out — controller likely disconnected.
        }
    }

    if (s_transfer_result < 0) {
        return false;
    }

    // The N64 controller read response is 4 bytes:
    //   [0] high byte of button word  (bits 15–8)
    //   [1] low  byte of button word  (bits  7–0)
    //   [2] stick X (signed)
    //   [3] stick Y (signed)
    // libjoybus DMA delivers them big-endian in s_response[].
    const auto *raw = reinterpret_cast<const joybus_n64_controller_input *>(s_response);
    state.buttons = raw->buttons;
    state.stick_x = static_cast<int8_t>(raw->stick_x);
    state.stick_y = static_cast<int8_t>(raw->stick_y);

    return true;
}

