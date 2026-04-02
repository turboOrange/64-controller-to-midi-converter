#include "midi.h"
#include "config.h"

#include "hardware/uart.h"
#include "hardware/gpio.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────

static inline void uart_write_byte(uint8_t b)
{
    uart_putc_raw(MIDI_UART_INSTANCE, static_cast<char>(b));
}

static inline void send2(uint8_t status, uint8_t data1)
{
    uart_write_byte(status);
    uart_write_byte(data1);
}

static inline void send3(uint8_t status, uint8_t data1, uint8_t data2)
{
    uart_write_byte(status);
    uart_write_byte(data1);
    uart_write_byte(data2);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void midi_init()
{
    uart_init(MIDI_UART_INSTANCE, MIDI_BAUD);
    gpio_set_function(PIN_MIDI_TX, GPIO_FUNC_UART);

    // Select the Ocarina patch at startup so the instrument is ready to play.
    midi_program_change(MIDI_CHANNEL, MIDI_PROGRAM);
}

void midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    send3(static_cast<uint8_t>(MidiStatus::NoteOn) | (channel & 0x0F), note & 0x7F, velocity & 0x7F);
}

void midi_note_off(uint8_t channel, uint8_t note)
{
    send3(static_cast<uint8_t>(MidiStatus::NoteOff) | (channel & 0x0F), note & 0x7F, 0x00);
}

void midi_control_change(uint8_t channel, uint8_t cc, uint8_t value)
{
    send3(static_cast<uint8_t>(MidiStatus::ControlChange) | (channel & 0x0F), cc & 0x7F, value & 0x7F);
}

void midi_program_change(uint8_t channel, uint8_t program)
{
    send2(static_cast<uint8_t>(MidiStatus::ProgramChange) | (channel & 0x0F), program & 0x7F);
}

void midi_pitch_bend(uint8_t channel, int16_t bend)
{
    // MIDI pitch bend is 14-bit, offset by 8192 so centre = 0x2000.
    uint16_t raw = static_cast<uint16_t>(bend + 8192);
    uint8_t  lsb = raw & 0x7F;
    uint8_t  msb = (raw >> 7) & 0x7F;
    send3(static_cast<uint8_t>(MidiStatus::PitchBend) | (channel & 0x0F), lsb, msb);
}

void midi_panic(uint8_t channel)
{
    midi_control_change(channel, static_cast<uint8_t>(MidiCC::AllNotesOff), 0x00);
}

void midi_realtime(MidiRealTime msg)
{
    uart_write_byte(static_cast<uint8_t>(msg));
}

