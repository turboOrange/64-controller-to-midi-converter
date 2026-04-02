#include "midi.h"
#include "config.h"

// midi_uart_lib already has extern "C" guards in its header.
#include "midi_uart_lib.h"

// ─── Instance ─────────────────────────────────────────────────────────────────

/// Opaque handle returned by midi_uart_configure(); held for the lifetime of
/// the firmware. Static allocation — no heap involvement.
static void *s_midi_uart = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────────────────

/// Queue one or more bytes into the IRQ-driven TX ring buffer and kick the
/// transmitter if it is idle. Non-blocking: returns immediately.
static inline void send(const uint8_t *buf, uint8_t len)
{
    midi_uart_write_tx_buffer(s_midi_uart, buf, len);
    midi_uart_drain_tx_buffer(s_midi_uart);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void midi_init()
{
    // Configure UART1 with IRQ-driven TX/RX ring buffers.
    // midi_uart_lib handles uart_init(), gpio_set_function(), and IRQ setup.
    s_midi_uart = midi_uart_configure(1, PIN_MIDI_TX, PIN_MIDI_RX);

    // Select the Ocarina patch at startup so the instrument is ready to play.
    midi_program_change(MIDI_CHANNEL, MIDI_PROGRAM);
}

void midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    uint8_t buf[3] = {
        static_cast<uint8_t>(static_cast<uint8_t>(MidiStatus::NoteOn) | (channel & 0x0F)),
        static_cast<uint8_t>(note     & 0x7F),
        static_cast<uint8_t>(velocity & 0x7F),
    };
    send(buf, 3);
}

void midi_note_off(uint8_t channel, uint8_t note)
{
    uint8_t buf[3] = {
        static_cast<uint8_t>(static_cast<uint8_t>(MidiStatus::NoteOff) | (channel & 0x0F)),
        static_cast<uint8_t>(note & 0x7F),
        0x00,
    };
    send(buf, 3);
}

void midi_control_change(uint8_t channel, uint8_t cc, uint8_t value)
{
    uint8_t buf[3] = {
        static_cast<uint8_t>(static_cast<uint8_t>(MidiStatus::ControlChange) | (channel & 0x0F)),
        static_cast<uint8_t>(cc    & 0x7F),
        static_cast<uint8_t>(value & 0x7F),
    };
    send(buf, 3);
}

void midi_program_change(uint8_t channel, uint8_t program)
{
    uint8_t buf[2] = {
        static_cast<uint8_t>(static_cast<uint8_t>(MidiStatus::ProgramChange) | (channel & 0x0F)),
        static_cast<uint8_t>(program & 0x7F),
    };
    send(buf, 2);
}

void midi_pitch_bend(uint8_t channel, int16_t bend)
{
    // MIDI pitch bend is 14-bit unsigned, offset by 8192 so centre = 0x2000.
    uint16_t raw = static_cast<uint16_t>(bend + 8192);
    uint8_t buf[3] = {
        static_cast<uint8_t>(static_cast<uint8_t>(MidiStatus::PitchBend) | (channel & 0x0F)),
        static_cast<uint8_t>(raw & 0x7F),          // LSB
        static_cast<uint8_t>((raw >> 7) & 0x7F),   // MSB
    };
    send(buf, 3);
}

void midi_panic(uint8_t channel)
{
    midi_control_change(channel, static_cast<uint8_t>(MidiCC::AllNotesOff), 0x00);
}

void midi_realtime(MidiRealTime msg)
{
    uint8_t b = static_cast<uint8_t>(msg);
    send(&b, 1);
}
