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
///
/// In SIMULATION mode the Renode virtual RP2040 models the SIO spinlock
/// registers (0xD0000100‥0xD000017F) as plain ArrayMemory, which always
/// returns 0 on reads.  A SIO spinlock read of 0 means "still taken by
/// another core", so spin_lock_unsafe_blocking() would loop forever —
/// deadlocking the firmware in midi_init() before any MIDI byte is ever
/// sent.  Single-core simulation has no actual inter-core contention, so
/// plain save_and_disable_interrupts() critical sections are sufficient.
#ifdef SIMULATION
  #define RING_BUFFER_MULTICORE_SUPPORT 0
#else
  #define RING_BUFFER_MULTICORE_SUPPORT 1
#endif

