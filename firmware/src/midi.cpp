#include "midi.h"
#include "config.h"

// midi_uart_lib already has extern "C" guards in its header.
#include "midi_uart_lib.h"

#include "FreeRTOS.h"
#include "semphr.h"

// ─── Instance ─────────────────────────────────────────────────────────────────

/// Opaque handle returned by midi_uart_configure(); held for the firmware lifetime.
static void *s_midi_uart = nullptr;

/// Mutex that serialises multi-byte MIDI writes across all controller tasks.
/// Without this, bytes from concurrent Note On messages on different channels
/// would be interleaved in the TX ring buffer and corrupt the MIDI stream.
static StaticSemaphore_t s_mutex_buf;
static SemaphoreHandle_t s_mutex = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────────────────

/// Queue bytes into the IRQ-driven TX ring buffer under the MIDI mutex.
/// Takes the mutex (blocking until free) so a complete multi-byte message
/// is written atomically — no interleaving between concurrent tasks.
static inline void send(const uint8_t *buf, uint8_t len)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    midi_uart_write_tx_buffer(s_midi_uart, buf, len);
    midi_uart_drain_tx_buffer(s_midi_uart);
    xSemaphoreGive(s_mutex);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void midi_init()
{
    // Create the MIDI write mutex using static storage (no heap allocation).
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);

    // Configure UART1 with IRQ-driven TX/RX ring buffers.
    s_midi_uart = midi_uart_configure(1, PIN_MIDI_TX, PIN_MIDI_RX);

    // Send an initial Program Change (Ocarina) on each active MIDI channel
    // so every controller starts with the correct GM patch.
    for (uint8_t i = 0; i < NUM_CONTROLLERS; ++i) {
        midi_program_change(CONTROLLER_CHANNELS[i], MIDI_PROGRAM);
    }
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
