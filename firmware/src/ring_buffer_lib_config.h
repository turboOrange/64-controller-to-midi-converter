/**
 * @file ring_buffer_lib_config.h
 * @brief Configuration for rppicomidi/ring_buffer_lib.
 *
 * Must be present in an include path visible to any translation unit that
 * includes ring_buffer_lib.h.  This file lives in firmware/src/ which is
 * already in target_include_directories for all project sources.
 *
 * Multicore support is required: the MIDI UART ISR (running on whichever
 * RP2040 core services the interrupt) drains the TX ring buffer concurrently
 * with FreeRTOS tasks that fill it.  Enabling multicore support ensures the
 * critical-section primitives use the RP2040 spin-lock mechanism rather than
 * a simple IRQ-disable that is only safe on a single core.
 */
#pragma once

/// Enable multi-core critical sections so the ring buffer is safe when
/// accessed from both RP2040 cores simultaneously.
#define RING_BUFFER_MULTICORE_SUPPORT 1

