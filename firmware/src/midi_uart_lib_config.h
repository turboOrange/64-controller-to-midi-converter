/**
 * @file midi_uart_lib_config.h
 * @brief Configuration for rppicomidi/midi_uart_lib.
 *
 * Must be present in an include path visible to the midi_uart_lib INTERFACE
 * target. The standard MIDI baud rate is 31 250; do not change unless you
 * have a very good reason.
 */
#pragma once

#define MIDI_UART_LIB_BAUD_RATE 31250

