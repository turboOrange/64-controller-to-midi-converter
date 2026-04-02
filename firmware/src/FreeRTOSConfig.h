/**
 * @file FreeRTOSConfig.h
 * @brief FreeRTOS kernel configuration for the RP2040 SMP (dual-core) port.
 *
 * Targets the FreeRTOS-Kernel RP2040 SMP port located at
 * firmware/lib/FreeRTOS-Kernel/portable/ThirdParty/GCC/RP2040/.
 *
 * Four controller tasks run concurrently across both Cortex-M0+ cores;
 * a single mutex serialises all MIDI UART writes so multi-byte messages
 * are never interleaved between channels.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ─── Clocks ───────────────────────────────────────────────────────────────────

/// RP2040 default system clock — 125 MHz.
#define configCPU_CLOCK_HZ              125000000UL

/// Scheduler tick at 1 kHz gives 1 ms granularity for vTaskDelayUntil().
#define configTICK_RATE_HZ              1000UL

// ─── SMP — dual-core ─────────────────────────────────────────────────────────

/// Use both Cortex-M0+ cores.
#define configNUMBER_OF_CORES           2

/// Allow tasks to be pinned to a specific core via vTaskCoreAffinitySet().
#define configUSE_CORE_AFFINITY         1

/// Let tasks of different priorities run simultaneously on different cores.
#define configRUN_MULTIPLE_PRIORITIES   1

// ─── Scheduler ────────────────────────────────────────────────────────────────

#define configUSE_PREEMPTION            1
#define configUSE_TIME_SLICING          1

/// Use 32-bit tick counter — no overflow concern at 1 kHz for embedded uptime.
#define configTICK_TYPE_WIDTH_IN_BITS   TICK_TYPE_WIDTH_32_BITS

/// Passive idle hook is not needed; the idle task simply sleeps (WFI).
#define configUSE_PASSIVE_IDLE_HOOK     0

/// Cortex-M0+ has CLZ but the RP2040 port is validated without the
/// optimised path — keep this at 0 to match the reference configuration.
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0

// ─── Task management ─────────────────────────────────────────────────────────

#define configMAX_PRIORITIES            8
#define configMINIMAL_STACK_SIZE        128     ///< words (512 B)
#define configMAX_TASK_NAME_LEN         12

// ─── Memory ───────────────────────────────────────────────────────────────────
// configSUPPORT_STATIC_ALLOCATION=1 and configKERNEL_PROVIDED_STATIC_MEMORY=1
// are injected automatically by the FreeRTOS-Kernel-Static CMake target.
// Dynamic allocation is kept available as a fallback; heap_4 is selected in
// CMakeLists.txt via FreeRTOS-Kernel-Heap4.

#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configTOTAL_HEAP_SIZE           (8 * 1024)

// ─── Safety hooks ─────────────────────────────────────────────────────────────

#define configUSE_IDLE_HOOK             0
#define configUSE_TICK_HOOK             0
#define configUSE_MALLOC_FAILED_HOOK    1   ///< Trap heap exhaustion in debug.
#define configCHECK_FOR_STACK_OVERFLOW  2   ///< Full watermark stack check.

// ─── Software timers ─────────────────────────────────────────────────────────

#define configUSE_TIMERS                1
#define configTIMER_TASK_PRIORITY       3
#define configTIMER_QUEUE_LENGTH        8
#define configTIMER_TASK_STACK_DEPTH    256  ///< words

// ─── Synchronisation ──────────────────────────────────────────────────────────

#define configUSE_MUTEXES               1
#define configUSE_RECURSIVE_MUTEXES     0
#define configUSE_COUNTING_SEMAPHORES   0
#define configUSE_TASK_NOTIFICATIONS    1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1

// ─── Pico SDK interop ────────────────────────────────────────────────────────
// configSUPPORT_PICO_SYNC_INTEROP is REQUIRED for SMP (configNUMBER_OF_CORES>1).
// The RP2040 port's prvFIFOInterruptHandler — which drives the inter-core
// yield mechanism — is compiled only when this flag is set.  The handler uses
// xEventGroupSetBitsFromISR(), which in turn requires INCLUDE_xTimerPendFunctionCall.

#define configSUPPORT_PICO_SYNC_INTEROP 1
#define configSUPPORT_PICO_TIME_INTEROP 0   ///< Not needed; we never call sleep_ms() from tasks.

// ─── Optional API inclusions ─────────────────────────────────────────────────

#define INCLUDE_vTaskDelay                    1
#define INCLUDE_vTaskDelayUntil               1
#define INCLUDE_xTaskGetCurrentTaskHandle     1
#define INCLUDE_xTaskNotifyStateClear         1
#define INCLUDE_xTaskNotify                   1
#define INCLUDE_vTaskDelete                   1
#define INCLUDE_vTaskSuspend                  1
#define INCLUDE_uxTaskGetStackHighWaterMark   1
/// Required so xEventGroupSetBitsFromISR() is declared in event_groups.h.
/// Used internally by the RP2040 SMP port's pico_sync interop layer.
#define INCLUDE_xTimerPendFunctionCall        1

#ifdef __cplusplus
}
#endif

