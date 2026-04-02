/**
 * @file controller_task.h
 * @brief Per-controller FreeRTOS task declaration.
 *
 * One ControllerContext is allocated (statically) per N64 controller.
 * Each context is passed as the task argument to controller_task_fn(),
 * which owns the entire poll → map → MIDI pipeline for that controller.
 *
 * Four tasks run concurrently across both RP2040 cores (SMP FreeRTOS),
 * mirroring the four ocarina buttons Link can hold simultaneously in
 * Ocarina of Time.
 */

#pragma once

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief Immutable per-controller identity, passed to the task at creation.
 *
 * Keeps the controller index (0–3) and its dedicated MIDI channel together
 * so the task function has a single coherent context object.
 */
struct ControllerContext {
    uint8_t idx;        ///< Controller index (0 … NUM_CONTROLLERS-1).
    uint8_t channel;    ///< MIDI output channel (0 … 15).
};

/**
 * @brief FreeRTOS task entry point for one N64 controller.
 *
 * Runs an infinite loop at POLL_INTERVAL_MS cadence:
 *   1. Blocks until the Joybus transfer completes (task notification from ISR).
 *   2. Runs the OoT-inspired MIDI mapping for this controller's channel.
 *
 * The task toggles the onboard LED on each successful poll of controller 0
 * as a system heartbeat.
 *
 * @param arg  Pointer to a ControllerContext (cast from void *).
 */
void controller_task_fn(void *arg);

#if MIDI_CLOCK_ENABLED
/**
 * @brief FreeRTOS task that emits MIDI real-time clock ticks at the
 *        configured BPM (MIDI_CLOCK_BPM from config.h).
 *
 * Sends one 0xF8 Clock byte every (60 000 000 / BPM / 24) µs, using
 * vTaskDelayUntil() for deterministic 1 ms-resolution timing.
 *
 * @param arg  Unused (pass nullptr).
 */
void midi_clock_task_fn(void *arg);
#endif

