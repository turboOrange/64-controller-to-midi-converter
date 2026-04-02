#pragma once
/*
 * Minimal stub to satisfy config.h's #include "hardware/pio.h" on a
 * non-Pico host build used by the unit-test suite.
 *
 * The PIO type and pio0 symbol are only ever *used* inside
 * n64_controller.cpp, which is NOT compiled as part of this host test
 * target — so the real values are never needed here.
 */
typedef void *PIO;
#define pio0 ((PIO)0)

