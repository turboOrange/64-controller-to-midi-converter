# 🎵 64-Controller-to-MIDI Converter

> *"The melody of the Ocarina flows from your hands — now it flows from your N64 controller."*

An embedded C++ firmware and KiCad hardware project that turns a **Nintendo 64 controller** into a fully-functional **MIDI instrument**, inspired by the ocarina gameplay mechanic of *The Legend of Zelda: Ocarina of Time*.

The RP2040 reads button and joystick inputs from up to **four simultaneous N64 controllers** via the Joybus protocol, maps them to MIDI notes and control messages, and outputs standard **5-pin DIN MIDI** at 31 250 baud — ready to plug into any synthesiser, DAW, or MIDI-capable instrument.

---

## Table of Contents

- [Features](#features)
- [Repository Layout](#repository-layout)
- [Hardware](#hardware)
  - [Pinout](#pinout)
  - [Schematic & PCB](#schematic--pcb)
  - [Bill of Materials (highlights)](#bill-of-materials-highlights)
- [MIDI Mapping](#midi-mapping)
  - [Notes](#notes)
  - [Control Messages](#control-messages)
  - [Velocity](#velocity)
  - [Default Tuning](#default-tuning)
- [Getting Started (Using the Converter)](#getting-started-using-the-converter)
- [Development](#development)
  - [Prerequisites](#prerequisites)
  - [Entering the Dev Shell](#entering-the-dev-shell)
  - [Building the Firmware](#building-the-firmware)
  - [Flashing](#flashing)
  - [Debugging](#debugging)
  - [Unit Tests](#unit-tests)
  - [Generating API Docs](#generating-api-docs)
  - [Configuration Reference](#configuration-reference)
- [Project Architecture](#project-architecture)
- [Key References](#key-references)

---

## Features

- 🎮 **Up to 4 simultaneous N64 controllers**, each on its own MIDI channel
- 🎼 **OoT-inspired note mapping** — A/B/C-buttons trigger ocarina notes in D major
- 🎛️ **Joystick → Pitch Bend / Modulation** — tilt for expression, just like blowing into the ocarina
- 🔇 **Z-button Sustain** — hold notes like a sustain pedal (CC64)
- 🎚️ **Octave shift** via L/R shoulder buttons (±3 octaves)
- 🎹 **D-pad Program Change** — cycle through General MIDI instruments
- ⏸️ **Start = MIDI Panic** — all notes off instantly
- ⏱️ **Optional MIDI Clock output** at configurable BPM
- 🔄 **Velocity sensitivity** driven by joystick distance from centre
- ⚡ **FreeRTOS SMP** on both RP2040 cores — all four controller pipelines run concurrently
- 🔌 **5-pin DIN MIDI** output (MIDI 1.0, 31 250 baud, optocoupler-isolated)

---

## Repository Layout

```
64-controller-to-midi-converter/
├── firmware/
│   ├── CMakeLists.txt          # CMake build definition
│   ├── src/
│   │   ├── main.cpp            # Entry point, FreeRTOS task launch
│   │   ├── config.h            # ← All user-tunable constants live here
│   │   ├── n64_controller.cpp/h  # Joybus PIO driver (wraps libjoybus)
│   │   ├── midi.cpp/h          # MIDI message builder + UART output
│   │   ├── mapping.cpp/h       # N64 input → MIDI note/CC mapping
│   │   ├── controller_task.cpp/h # Per-controller FreeRTOS task
│   │   └── FreeRTOSConfig.h    # FreeRTOS kernel configuration
│   ├── pio/                    # PIO assembler programs (.pio)
│   ├── lib/
│   │   ├── libjoybus/          # Joybus PIO driver (git submodule)
│   │   ├── midi_uart_lib/      # IRQ-driven MIDI UART (git submodule)
│   │   ├── ring_buffer_lib/    # Ring buffer for midi_uart_lib (git submodule)
│   │   └── FreeRTOS-Kernel/    # FreeRTOS V11 RP2040 SMP port (git submodule)
│   └── tests/                  # Host-side unit tests
├── hardware/
│   ├── hardware.kicad_sch      # KiCad schematic
│   ├── hardware.kicad_pcb      # KiCad PCB layout
│   └── modules/                # Local KiCad sub-sheets
├── flake.nix                   # Nix dev shell (reproduces full toolchain)
└── flake.lock                  # Locked Nixpkgs revision
```

---

## Hardware

### Pinout

The firmware targets a minimal custom RP2040 PCB. All GPIO assignments are defined in [`firmware/src/config.h`](firmware/src/config.h).

#### N64 Controller Data Lines (Joybus, open-drain)

Each controller uses a **single bidirectional data pin** (1 µs bit timing, 3.3 V logic):

| Controller | GPIO | Notes |
|:---:|:---:|---|
| Controller 1 | **GP2** | MIDI Channel 1 |
| Controller 2 | **GP3** | MIDI Channel 2 |
| Controller 3 | **GP6** | MIDI Channel 3 |
| Controller 4 | **GP7** | MIDI Channel 4 |

> ⚠️ GP4 and GP5 are reserved for MIDI UART — keep those traces clear of the Joybus lines.

Each Joybus line needs a **3.3 V pull-up resistor** (typically 1 kΩ–4.7 kΩ) and connects directly to pin 3 of the N64 controller plug.

**N64 Controller Plug Pinout (looking into the plug from the front):**

```
   ┌─────────────────┐
   │  1    2    3    │
   │  GND  +3.3V DATA│
   └─────────────────┘
```

| Pin | Signal |
|:---:|---|
| 1 | GND |
| 2 | 3.3 V power (from the PCB's LDO output) |
| 3 | Data (Joybus, bidirectional, open-drain) → GPx |

#### MIDI DIN Output (UART1)

| Signal | GPIO | Notes |
|---|:---:|---|
| MIDI TX | **GP4** | UART1 TX → optocoupler → 5-pin DIN pin 5 |
| MIDI RX | **GP5** | UART1 RX (leave unconnected if MIDI IN not needed) |

MIDI DIN wiring follows the **MIDI 1.0 spec**: 220 Ω current-limiting resistors on the TX side, 6N138 (or equivalent) optocoupler on the RX side.

#### Status LED

| Signal | GPIO |
|---|:---:|
| Onboard LED / heartbeat | **GP25** |

The LED blinks once per controller-0 poll cycle (~100 Hz) as a heartbeat. Distinct blink patterns indicate FreeRTOS fault conditions:
- **Fast blink (50 ms)** — heap allocation failure
- **Slow blink (200 ms)** — task stack overflow

#### Power

| Rail | Source |
|---|---|
| 5 V | USB connector |
| 3.3 V | LDO regulator (supplies MCU, controller plugs, and pull-ups) |

---

### Schematic & PCB

KiCad 7+ source files are in [`hardware/`](hardware/). The design targets a **2-layer, hand-solderable** board (through-hole + 0805 SMD minimum). Open `hardware.kicad_pro` in KiCad to view the full schematic and PCB layout.

---

### Bill of Materials (highlights)

| Component | Value / Part | Qty |
|---|---|:---:|
| MCU | RP2040 (LQFP-56 or QFN-56) | 1 |
| LDO regulator | 3.3 V, ≥ 500 mA (e.g. MCP1700-3302E) | 1 |
| Crystal | 12 MHz (if not using USB internal oscillator) | 1 |
| Flash | W25Q16JV or compatible (2 MB SPI) | 1 |
| Optocoupler (MIDI OUT) | 6N138 | 1 |
| N64 controller sockets | DE-9 or custom 3-pin JST | 1–4 |
| MIDI DIN socket | 5-pin 180° DIN (Kycon KMDGX-5S-BS or similar) | 1 |
| Pull-up resistors (Joybus) | 1 kΩ, 0805 | 4 |
| Decoupling capacitors | 100 nF, 0805 | ×8 |

---

## MIDI Mapping

All mappings are OoT-inspired and reconfigurable in `config.h`.

### Notes

The root note defaults to **D4 (MIDI 62)** to match the D-major palette of *Ocarina of Time*. The note buttons map to scale degrees of the D major scale:

| N64 Button | MIDI Note (default) | Interval | OoT Reference |
|---|:---:|---|---|
| **A** | D4 (62) | Root | Ocarina A |
| **B** | A4 (69) | Perfect 5th | Ocarina B |
| **C-Up** | E4 (64) | Major 2nd | C-Up melody note |
| **C-Down** | F#4 (66) | Major 3rd | C-Down melody note |
| **C-Left** | A4 (69) | Perfect 5th | C-Left melody note |
| **C-Right** | B4 (71) | Major 6th | C-Right melody note |

Multiple note buttons can be held simultaneously, enabling chords — just like Link can hold multiple ocarina buttons to play harmony.

### Control Messages

| N64 Input | MIDI Message | Details |
|---|---|---|
| **Joystick X/Y** | Pitch Bend or CC1 (Modulation) | Within deadzone (±8): no output. Beyond deadzone: mapped linearly to ±8191 pitch bend range. |
| **Z button** | CC64 (Sustain Pedal) | Press = sustain on; release = sustain off |
| **L shoulder** | Octave shift down | Shifts all notes down one octave per press (min −3 octaves) |
| **R shoulder** | Octave shift up | Shifts all notes up one octave per press (max +3 octaves) |
| **Start** | CC123 (All Notes Off) — MIDI Panic | Sends all-notes-off on all 16 channels; resets all state |
| **D-pad Up/Down** | Program Change | Cycles through GM programs; defaults to GM #80 (Ocarina) |
| **D-pad Left/Right** | Program Change | Alternative instrument select direction |

### Velocity

When a note button is pressed, **velocity is derived from joystick distance from centre** at the moment of the press:

- Joystick at rest (within deadzone) → velocity **64** (mezzo-forte)
- Joystick pushed to maximum (±85 raw units) → velocity **127** (fortissimo)
- Velocity is scaled linearly between deadzone and `JOYSTICK_MAX`

This replicates the feel of blowing harder into the ocarina for louder notes.

### Default Tuning

| Setting | Default | Notes |
|---|---|---|
| Root note | D4 (MIDI 62) | Matches OoT's D-major palette |
| Default GM program | 80 (Ocarina) | Set on startup for all active channels |
| Joystick dead-zone | ±8 | Raw N64 axis units (range ≈ ±85) |
| MIDI Clock | Disabled | Enable via `MIDI_CLOCK_ENABLED` in `config.h` |
| BPM (when clock enabled) | 120 | Configurable via `MIDI_CLOCK_BPM` |
| Poll rate | 100 Hz (10 ms) | Matches OoT's original controller polling cadence |

---

## Getting Started (Using the Converter)

1. **Flash the firmware** (see [Flashing](#flashing) below).

2. **Connect your N64 controller(s)** to the appropriate GPIO pins via the 3-pin connector:
   - Pin 1 → GND
   - Pin 2 → 3.3 V
   - Pin 3 → GPx (see [Pinout](#pinout))

3. **Connect a MIDI device** to the 5-pin DIN MIDI OUT jack — a synthesiser, a DAW audio interface with MIDI IN, or a USB-MIDI adapter.

4. **Power the board** via USB. The onboard LED will begin blinking to confirm the firmware is running.

5. **Play:**
   - Press **A** to sound the root note (D4)
   - Press **C-buttons** to play other scale degrees
   - Tilt the **joystick** while holding a note for pitch bend / vibrato
   - Hold **Z** to sustain notes
   - Press **L/R** to shift octaves
   - Use the **D-pad** to change instruments
   - Press **Start** to silence everything (MIDI Panic)

6. **Use multiple controllers** simultaneously — each is pre-assigned to its own MIDI channel (CH 1–4), so four players can jam together or you can layer sounds on a multi-timbral synth.

---

## Development

### Prerequisites

The project ships a fully reproducible development environment via **Nix flakes**. All required tools are pinned and hermetic — no manual SDK installation needed.

Alternatively, install the following manually:

| Tool | Version | Purpose |
|---|---|---|
| `arm-none-eabi-gcc` | ≥ 13 | ARM bare-metal toolchain |
| `cmake` | ≥ 3.21 | Build system |
| `ninja` | any | Fast parallel builds |
| Raspberry Pi Pico SDK | ≥ 2.0 | SDK + `pioasm` |
| `python3` | ≥ 3.9 | Required by Pico SDK CMake scripts |
| `picotool` | any | Flash / inspect RP2040 over USB |
| `openocd` | any | SWD debugging |
| `kicad` | ≥ 7 | Hardware design |
| `doxygen` | any | API documentation generation |

---

### Entering the Dev Shell

```bash
# First time — lock the Nixpkgs revision
nix flake lock

# Enter the hermetic dev shell (downloads toolchain on first run)
nix develop
```

The shell prints a summary of active tool versions and common commands on entry.

**Auto-activation with `direnv`** (recommended):

```bash
echo "use flake" > .envrc
direnv allow
```

The dev shell will activate automatically every time you `cd` into the project.

---

### Building the Firmware

From inside the dev shell (or with the tools on your `PATH`):

```bash
cd firmware

# Configure (only needed once, or after CMakeLists changes)
cmake -B build -G Ninja

# Build
ninja -C build
```

The build produces `firmware/build/n64_midi_converter.uf2` (and `.elf`, `.hex`, `.bin`, `.dis`).

**Out-of-shell build** (passing the SDK path explicitly):

```bash
cmake -B build -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk
ninja -C build
```

**Initialising git submodules** (first-time clone only):

```bash
git submodule update --init --recursive
```

---

### Interactive MIDI Tester (no hardware required)

A native host-side tool lets you press buttons on a virtual N64 controller and verify every resulting MIDI event — no RP2040, no physical controller needed.

```
┌─ N64 Controller ─────────────────────────┐┌─ MIDI Events (newest at bottom) ──────────┐
│ Octave shift: +0                         ││ INIT      Program Change GM#80 ch 0-3     │
│  [L](Q)                        [R](E)    ││                                            │
│       [^]   .---.  [C^](I)  [A](J)      ││ NOTE ON   ch0   D4  (62)  vel 80  90 3E 50│
│  [<]  [>]   | o |  [C<](U) [C>](O)      ││ PITCH BND ch0   +2730     E0 2A 55        │
│       [v]   '---'  [Cv](M)  [B](K)      ││ NOTE OFF  ch0   D4  (62)  80 3E 00        │
│  [Z](Space)       [Start](Enter)         ││                                            │
│ Stick: X=  +0  Y= +85                   ││                                            │
└──────────────────────────────────────────┘└────────────────────────────────────────────┘
 64-to-MIDI Tester  |  ch 0  |  oct +0  |  events: 4  |  toggle keys on/off  |  Esc=quit
```

Active buttons light up **green**. The right panel shows colour-coded decoded MIDI events with their raw hex bytes.

**Key layout (all keys toggle on/off):**

| Key | N64 Input | Key | N64 Input |
|---|---|---|---|
| `J` | A button | `K` | B button |
| `I` | C-Up | `M` | C-Down |
| `U` | C-Left | `O` | C-Right |
| `Space` | Z (sustain) | `Enter` | Start (MIDI Panic) |
| `Q` | L shoulder (octave −) | `E` | R shoulder (octave +) |
| Arrow keys | D-pad | `W`/`A`/`S`/`D` | Joystick axes |
| `Esc` | Quit | | |

**Build and run:**

```bash
cd firmware/tools/n64_midi_tester
cmake -B build -G Ninja
ninja -C build
./build/n64_midi_tester
```

**Optional — pipe MIDI to `fluidsynth` for audio:**

```bash
mkfifo /tmp/midi_pipe
fluidsynth -a alsa -g 1.0 /usr/share/soundfonts/FluidR3_GM.sf2 /tmp/midi_pipe &
./build/n64_midi_tester --midi-out /tmp/midi_pipe
```

---

### Flashing

#### Method 1 — UF2 drag-and-drop / picotool

1. Hold **BOOTSEL** while connecting the board via USB (or press BOOTSEL + RUN).
2. The board mounts as a mass-storage device (`RPI-RP2`).

```bash
picotool load -f firmware/build/n64_midi_converter.uf2
# or simply copy the .uf2 to the mounted drive
```

#### Method 2 — SWD (OpenOCD + Raspberry Pi Debug Probe)

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "program firmware/build/n64_midi_converter.elf verify reset exit"
```

---

### Debugging

**GDB over SWD** (requires a Raspberry Pi Debug Probe or J-Link):

```bash
# Terminal 1 — start OpenOCD GDB server
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg

# Terminal 2 — connect GDB
arm-none-eabi-gdb firmware/build/n64_midi_converter.elf \
  -ex "target extended-remote :3333" \
  -ex "monitor reset halt" \
  -ex "load"
```

**UART serial monitor** (if `stdio_uart` is re-enabled for debugging):

```bash
minicom -b 115200 -D /dev/ttyACM0
```

> Note: UART1 is fully owned by `midi_uart_lib` for MIDI output. Re-enable `stdio_usb` in `CMakeLists.txt` for debug `printf` output over the USB CDC serial port instead.

---

### Unit Tests

The `firmware/tests/` directory contains a host-side [Catch2 v3](https://github.com/catchorg/Catch2) test suite that exercises the core `Mapping` logic without any RP2040, Joybus, or FreeRTOS dependencies.

**What is tested:**

| Tag | Coverage |
|---|---|
| `[note_buttons]` | A / B / C-Up / C-Down / C-Left / C-Right → Note On / Note Off with correct pitches |
| `[octave]` | L / R shoulder octave shift, ±3 clamp, note-off snapshot correctness |
| `[sustain]` | Z button → CC64 on / off |
| `[panic]` | Start → CC123, octave and note-tracking reset |
| `[dpad]` | D-pad directions → correct GM Program Change numbers |
| `[joystick]` | Pitch Bend (X-axis), Modulation (Y-axis), deadzone, wire encoding (14-bit LSB/MSB) |
| `[velocity]` | Joystick distance → MIDI velocity scaling, range validation |
| `[channel]` | All events carry the channel bound at `Mapping` construction |
| `[edge_cases]` | Simultaneous six-button chords, held-button idempotency, 7-bit data byte bounds |

**Build and run (inside `nix develop`):**

```bash
cd firmware/tests
cmake -B build -G Ninja
ninja -C build
./build/n64_midi_tests            # run all tests
./build/n64_midi_tests [octave]   # run a single tag
./build/n64_midi_tests -v         # verbose output
```

**With CTest:**

```bash
cd firmware/tests/build
ctest --output-on-failure
```

Catch2 v3 is provided as a system package inside the `nix develop` shell.  In environments without Nix the `CMakeLists.txt` automatically fetches it from GitHub via `FetchContent`.

---

### Generating API Docs

```bash
cd firmware/build
cmake --build . --target docs    # if a docs target is configured
# or directly:
doxygen ../CMakeDoxyfile.in
```

HTML documentation is generated in `firmware/build/docs/html/`.

---

### Configuration Reference

All user-tunable parameters live in **[`firmware/src/config.h`](firmware/src/config.h)**. Edit this file to customise the converter without changing any logic.

| Constant | Default | Description |
|---|:---:|---|
| `NUM_CONTROLLERS` | `4` | Number of N64 controllers to support (1–4) |
| `CONTROLLER_PINS[]` | `{2, 3, 6, 7}` | GPIO pins for each controller's Joybus data line |
| `CONTROLLER_CHANNELS[]` | `{0, 1, 2, 3}` | MIDI channel (0-indexed) per controller |
| `JOYBUS_PIO_INSTANCE` | `pio0` | PIO block used for all Joybus state machines |
| `PIN_MIDI_TX` | `4` | GPIO for MIDI UART TX |
| `PIN_MIDI_RX` | `5` | GPIO for MIDI UART RX |
| `PIN_LED` | `25` | GPIO for status LED |
| `MIDI_UART_INSTANCE` | `uart1` | UART peripheral for DIN MIDI |
| `MIDI_BAUD` | `31250` | MIDI baud rate (do not change) |
| `MIDI_PROGRAM` | `79` | Default GM program (79 = GM #80, Ocarina, 0-indexed) |
| `NOTE_ROOT` | `62` | Root note (A button) — default D4 |
| `NOTE_B` | `69` | B button note — default A4 (perfect 5th) |
| `NOTE_C_UP` | `64` | C-Up note — default E4 (+2 semitones) |
| `NOTE_C_DOWN` | `66` | C-Down note — default F#4 (+4 semitones) |
| `NOTE_C_LEFT` | `69` | C-Left note — default A4 (+7 semitones) |
| `NOTE_C_RIGHT` | `71` | C-Right note — default B4 (+9 semitones) |
| `JOYSTICK_DEADZONE` | `8` | Joystick dead-zone radius (raw units, 0–127) |
| `JOYSTICK_MAX` | `85` | Maximum joystick axis magnitude (raw units) |
| `MIDI_CLOCK_ENABLED` | `false` | Enable MIDI real-time clock output |
| `MIDI_CLOCK_BPM` | `120` | BPM for MIDI clock when enabled |
| `POLL_INTERVAL_MS` | `10` | Controller poll interval (10 ms = 100 Hz) |
| `CONTROLLER_TASK_STACK_WORDS` | `512` | FreeRTOS stack size per controller task (words) |
| `CONTROLLER_TASK_PRIORITY` | `2` | FreeRTOS priority for controller tasks |

---

## Project Architecture

```
                   ┌──────────────────────────────────┐
                   │           RP2040 Firmware         │
                   │                                   │
  N64 Controller ──► PIO State Machine (Joybus)        │
   (Joybus, 1 MHz) │   │  libjoybus (submodule)        │
                   │   ▼                               │
                   │ n64_controller_poll()  ──►  N64State
                   │                               │   │
                   │                          mapping.cpp
                   │                     (OoT note logic)
                   │                               │   │
                   │                          midi.cpp   │
                   │                     (MIDI builder)  │
                   │                               │   │
                   │                  midi_uart_lib (IRQ TX)
                   │                               │   │
                   └───────────────────────────────┼───┘
                                                   │
                                          UART1, GP4 (TX)
                                                   │
                                         ┌─────────▼─────────┐
                                         │  5-pin DIN MIDI   │
                                         │  OUT (31250 baud) │
                                         └───────────────────┘
                                                   │
                                          Synthesiser / DAW

  FreeRTOS SMP scheduler distributes four controller tasks
  across both RP2040 Cortex-M0+ cores.
```

### Key Libraries (git submodules)

| Domain | Library | Path |
|---|---|---|
| Joybus (N64 protocol) | [`loopj/libjoybus`](https://github.com/loopj/libjoybus) | `firmware/lib/libjoybus` |
| DIN MIDI UART | [`rppicomidi/midi_uart_lib`](https://github.com/rppicomidi/midi_uart_lib) | `firmware/lib/midi_uart_lib` |
| Ring buffer | [`rppicomidi/ring_buffer_lib`](https://github.com/rppicomidi/ring_buffer_lib) | `firmware/lib/ring_buffer_lib` |
| RTOS | [`FreeRTOS-Kernel`](https://github.com/FreeRTOS/FreeRTOS-Kernel) V11 (RP2040 SMP) | `firmware/lib/FreeRTOS-Kernel` |
| USB MIDI | TinyUSB (bundled with Pico SDK) | via `tinyusb_device` CMake target |

---

## Key References

- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Raspberry Pi Pico SDK Documentation](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html)
- [N64 Joybus Protocol — n64brew.dev](https://n64brew.dev/wiki/Joybus_Protocol)
- [MIDI 1.0 Core Specification](https://midi.org/midi-1-0-core-specifications)
- [TinyUSB MIDI device example](https://github.com/hathach/tinyusb/tree/master/examples/device/midi_test)
- [loopj/libjoybus](https://github.com/loopj/libjoybus)
- [rppicomidi/midi_uart_lib](https://github.com/rppicomidi/midi_uart_lib)
- [KiCad Documentation](https://docs.kicad.org/)

---

*Made with 🎵 and 🎮 — for everyone who ever wished they could play Saria's Song on real hardware.*

