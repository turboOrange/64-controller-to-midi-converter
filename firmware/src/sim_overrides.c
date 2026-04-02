/**
 * @file sim_overrides.c
 * @brief Simulation-only overrides for Pico SDK __weak init functions.
 *
 * Compiled exclusively when SIMULATION=1 (the n64_midi_converter_sim target).
 * Each function here overrides a Pico SDK __weak symbol that either
 * spin-waits on hardware Renode cannot faithfully model or calls into the
 * RP2040 boot ROM which is not mapped in rp2040_sim.repl.
 *
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │ runtime_init_clocks                                                  │
 * │   The SDK version polls clock-selected registers waiting for the     │
 * │   value 0x1  (e.g. `while (clocks_hw->clk[clk_sys].selected != 1)`) │
 * │   Our ArrayMemory / Tag stubs return 0xFFFFFFFF ≠ 0x1 → hangs      │
 * │   forever before main() is reached.  The Renode NVIC model drives   │
 * │   SysTick directly from systickFrequency, so real clock init is      │
 * │   unnecessary in simulation.                                         │
 * │                                                                      │
 * │ runtime_init_bootrom_reset                                           │
 * │ runtime_init_per_core_bootrom_reset                                  │
 * │   Both call rom_func_lookup() which reads a function-pointer table   │
 * │   from the RP2040 boot ROM at 0x00000000.  That address is not       │
 * │   mapped in rp2040_sim.repl → HardFault before main() starts.       │
 * └──────────────────────────────────────────────────────────────────────┘
 *
 * The linker prefers non-weak definitions, so these stubs silently win
 * over the SDK versions at link time without patching any library.
 */

/* Silence clang "missing prototype" warnings — these match SDK prototypes
 * declared in pico/runtime_init.h but we intentionally avoid that include
 * to keep this file dependency-free. */
#pragma GCC diagnostic ignored "-Wmissing-prototypes"

/**
 * @brief No-op clock initialisation for Renode simulation.
 *
 * The real SDK version configures XOSC, PLLs, and all clock muxes then
 * spin-waits for hardware acknowledge bits.  None of those registers are
 * faithfully modelled in the minimal RP2040 platform description, so we
 * skip the whole sequence.  FreeRTOS's SysTick is driven by the NVIC
 * model (systickFrequency: 125000000 in rp2040_sim.repl) and does not
 * depend on the hardware tick generator that real clock init would start.
 */
void runtime_init_clocks(void) { /* no-op */ }

/**
 * @brief No-op boot ROM global-state reset for Renode simulation.
 *
 * The real SDK version resolves a function pointer from the RP2040 boot
 * ROM lookup table at 0x00000000.  That region is unmapped in our Renode
 * platform, so the ROM fetch would raise a HardFault.  We skip it because
 * the Renode simulation starts in a known clean state anyway.
 */
void runtime_init_bootrom_reset(void) { /* no-op */ }

/**
 * @brief No-op per-core boot ROM state reset for Renode simulation.
 *
 * Same rationale as runtime_init_bootrom_reset above.
 */
void runtime_init_per_core_bootrom_reset(void) { /* no-op */ }

/**
 * @brief No-op USB power-down for Renode simulation.
 *
 * The real SDK version does a read-modify-write on usb_hw->muxing
 * (USB controller base 0x50110000), which is not mapped in rp2040_sim.repl.
 * An unmapped read raises a HardFault on Cortex-M0+ before main() starts.
 * USB is fully disabled for the sim build anyway (pico_enable_stdio_usb 0).
 */
void runtime_init_usb_power_down(void) { /* no-op */ }

