/**
 * @file main.cpp
 * @brief Interactive N64 controller simulator and MIDI event verifier.
 *
 * Compiles firmware/src/mapping.cpp natively on x86/x64 with stub
 * implementations of all midi_* functions.  Instead of driving a hardware
 * UART, the stubs push colour-coded, decoded MIDI events into an ncurses
 * log panel so you can verify the mapping logic in real-time without any
 * RP2040 or N64 hardware.
 *
 * Input model — TOGGLE
 * ────────────────────
 * Because terminals do not deliver key-release events, every mapped key
 * acts as a toggle: press once to engage that button (Note On fires),
 * press the same key again to release it (Note Off fires).  This means
 * you can hold a chord by pressing several keys and they will stay lit
 * until you press them again.  Joystick axes work the same way: pressing
 * W moves the stick to full-up; pressing W again snaps it back to centre.
 *
 * Key layout
 * ──────────
 *   J       = A button          K       = B button
 *   I       = C-Up              M       = C-Down
 *   U       = C-Left            O       = C-Right
 *   Space   = Z  (sustain)      Enter   = Start (MIDI Panic + state reset)
 *   Q       = L shoulder  (octave −)
 *   E       = R shoulder  (octave +)
 *   Arrows  = D-pad             W/A/S/D = Joystick axes (toggle)
 *   Escape  = Quit
 *
 * Optional MIDI byte output
 * ─────────────────────────
 *   Pass --midi-out <path> to write raw MIDI bytes to a file or named pipe.
 *   Example — pipe to fluidsynth for audio:
 *     mkfifo /tmp/midi
 *     fluidsynth -a alsa -g 1.0 /path/to/soundfont.sf2 /tmp/midi &
 *     ./build/n64_midi_tester --midi-out /tmp/midi
 */

#include "mapping.h"    // Mapping class — real production code
#include "midi.h"       // midi_* declarations  (stubs defined below)
#include "config.h"     // NOTE_ROOT, JOYSTICK_MAX, etc.

extern "C" {
#include <joybus/n64.h> // JOYBUS_N64_BUTTON_* bitmasks — plain #defines, no SDK
}

#include <ncurses.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <thread>

// ─── Optional raw MIDI byte output ───────────────────────────────────────────

static FILE *g_midi_out = nullptr;

static void write_raw(const uint8_t *buf, int len)
{
    if (!g_midi_out) return;
    fwrite(buf, 1, len, g_midi_out);
    fflush(g_midi_out);
}

// ─── MIDI event log ───────────────────────────────────────────────────────────

struct LogEntry {
    std::string text;   ///< Human-readable decoded message
    std::string hex;    ///< Raw hex bytes, e.g. "90 3E 50"
    short       color;  ///< ncurses color-pair index
};

static std::deque<LogEntry> g_log;
static constexpr std::size_t LOG_MAX = 500;

static void log_push(std::string text, std::string hex, short color)
{
    if (g_log.size() >= LOG_MAX) g_log.pop_front();
    g_log.push_back({std::move(text), std::move(hex), color});
}

/** Convert a MIDI note number to a name string: 62 → "D4", 60 → "C4". */
static const char *note_name(uint8_t note)
{
    static const char *names[] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    static char buf[8];
    snprintf(buf, sizeof(buf), "%s%d", names[note % 12], (note / 12) - 1);
    return buf;
}

// ncurses color-pair indices
enum : short {
    CP_NOTE_ON  = 1,   // green  — Note On
    CP_NOTE_OFF = 2,   // red    — Note Off
    CP_CC       = 3,   // cyan   — Control Change
    CP_PC       = 4,   // yellow — Program Change
    CP_BEND     = 5,   // magenta — Pitch Bend
    CP_RT       = 6,   // white  — Real-time
    CP_HEADER   = 7,   // white on blue — status bar
    CP_DIM      = 8,   // dim white — inactive labels
    CP_BTN_ON   = 9,   // bold green — active button in controller view
};

// ─── MIDI stub implementations ────────────────────────────────────────────────
// These replace midi.cpp entirely for this host build.  Each function records
// a decoded entry in g_log and optionally writes raw bytes to g_midi_out.

void midi_init()
{
    log_push("INIT      Program Change GM#80 (Ocarina) on ch 0-3",
             "C0 4F  C1 4F  C2 4F  C3 4F", CP_PC);
    const uint8_t pcs[] = {0xC0,0x4F, 0xC1,0x4F, 0xC2,0x4F, 0xC3,0x4F};
    write_raw(pcs, sizeof(pcs));
}

void midi_note_on(uint8_t ch, uint8_t note, uint8_t vel)
{
    char text[80], hex[16];
    snprintf(text, sizeof(text), "NOTE ON   ch%-2u  %-4s (%3u)  vel %u",
             ch, note_name(note), note, vel);
    snprintf(hex,  sizeof(hex),  "%02X %02X %02X",
             0x90 | (ch & 0xF), note & 0x7F, vel & 0x7F);
    log_push(text, hex, CP_NOTE_ON);
    const uint8_t buf[3] = {
        uint8_t(0x90 | (ch & 0xF)), uint8_t(note & 0x7F), uint8_t(vel & 0x7F)
    };
    write_raw(buf, 3);
}

void midi_note_off(uint8_t ch, uint8_t note)
{
    char text[80], hex[16];
    snprintf(text, sizeof(text), "NOTE OFF  ch%-2u  %-4s (%3u)",
             ch, note_name(note), note);
    snprintf(hex,  sizeof(hex),  "%02X %02X 00",
             0x80 | (ch & 0xF), note & 0x7F);
    log_push(text, hex, CP_NOTE_OFF);
    const uint8_t buf[3] = {
        uint8_t(0x80 | (ch & 0xF)), uint8_t(note & 0x7F), 0x00
    };
    write_raw(buf, 3);
}

void midi_control_change(uint8_t ch, uint8_t cc, uint8_t val)
{
    const char *cc_name =
        cc ==   1 ? "Mod Wheel"   :
        cc ==  64 ? "Sustain"     :
        cc == 123 ? "AllNotesOff" : "CC";
    char text[80], hex[16];
    snprintf(text, sizeof(text), "CC        ch%-2u  #%-3u %-13s  val %u",
             ch, cc, cc_name, val);
    snprintf(hex,  sizeof(hex),  "%02X %02X %02X",
             0xB0 | (ch & 0xF), cc & 0x7F, val & 0x7F);
    log_push(text, hex, (cc == 123) ? CP_NOTE_OFF : CP_CC);
    const uint8_t buf[3] = {
        uint8_t(0xB0 | (ch & 0xF)), uint8_t(cc & 0x7F), uint8_t(val & 0x7F)
    };
    write_raw(buf, 3);
}

void midi_program_change(uint8_t ch, uint8_t prog)
{
    char text[80], hex[12];
    snprintf(text, sizeof(text), "PROG CHG  ch%-2u  #%u  (GM %u)",
             ch, prog, prog + 1);
    snprintf(hex, sizeof(hex), "%02X %02X",
             0xC0 | (ch & 0xF), prog & 0x7F);
    log_push(text, hex, CP_PC);
    const uint8_t buf[2] = {uint8_t(0xC0 | (ch & 0xF)), uint8_t(prog & 0x7F)};
    write_raw(buf, 2);
}

void midi_pitch_bend(uint8_t ch, int16_t bend)
{
    // Suppress zero-bend log noise (fires every frame when stick is centred).
    // Raw bytes are still piped so the connected synth resets pitch correctly.
    const uint16_t raw = uint16_t(bend + 8192);
    const uint8_t buf[3] = {
        uint8_t(0xE0 | (ch & 0xF)),
        uint8_t(raw & 0x7F),
        uint8_t((raw >> 7) & 0x7F)
    };
    write_raw(buf, 3);

    if (bend == 0) return;

    char text[80], hex[16];
    snprintf(text, sizeof(text), "PITCH BND ch%-2u  %+d", ch, bend);
    snprintf(hex,  sizeof(hex),  "%02X %02X %02X", buf[0], buf[1], buf[2]);
    log_push(text, hex, CP_BEND);
}

void midi_panic(uint8_t ch)
{
    midi_control_change(ch, 123, 0);
}

void midi_realtime(MidiRealTime msg)
{
    const char *name =
        msg == MidiRealTime::Clock    ? "Clock"    :
        msg == MidiRealTime::Start    ? "Start"    :
        msg == MidiRealTime::Stop     ? "Stop"     :
        msg == MidiRealTime::Continue ? "Continue" : "RT";
    char text[40], hex[8];
    snprintf(text, sizeof(text), "REALTIME  %s", name);
    snprintf(hex,  sizeof(hex),  "%02X", uint8_t(msg));
    log_push(text, hex, CP_RT);
    const uint8_t b = uint8_t(msg);
    write_raw(&b, 1);
}

// ─── Key → N64 input table ────────────────────────────────────────────────────

struct KeyEntry {
    int        key;       ///< ncurses key code
    uint16_t   btn_mask;  ///< JOYBUS_N64_BUTTON_* flag, or 0 for stick axis
    int8_t     sx;        ///< Joystick X contribution when toggled on
    int8_t     sy;        ///< Joystick Y contribution when toggled on
};

static const KeyEntry KEY_TABLE[] = {
    // ── Note / action buttons ─────────────────────────────────────────────
    { 'j',       JOYBUS_N64_BUTTON_A,       0,    0 },
    { 'k',       JOYBUS_N64_BUTTON_B,       0,    0 },
    { 'i',       JOYBUS_N64_BUTTON_C_UP,    0,    0 },
    { 'm',       JOYBUS_N64_BUTTON_C_DOWN,  0,    0 },
    { 'u',       JOYBUS_N64_BUTTON_C_LEFT,  0,    0 },
    { 'o',       JOYBUS_N64_BUTTON_C_RIGHT, 0,    0 },
    // ── Control ──────────────────────────────────────────────────────────
    { ' ',       JOYBUS_N64_BUTTON_Z,       0,    0 },
    { '\n',      JOYBUS_N64_BUTTON_START,   0,    0 },
    { KEY_ENTER, JOYBUS_N64_BUTTON_START,   0,    0 },
    { 'q',       JOYBUS_N64_BUTTON_L,       0,    0 },
    { 'e',       JOYBUS_N64_BUTTON_R,       0,    0 },
    // ── D-pad ─────────────────────────────────────────────────────────────
    { KEY_UP,    JOYBUS_N64_BUTTON_UP,      0,    0 },
    { KEY_DOWN,  JOYBUS_N64_BUTTON_DOWN,    0,    0 },
    { KEY_LEFT,  JOYBUS_N64_BUTTON_LEFT,    0,    0 },
    { KEY_RIGHT, JOYBUS_N64_BUTTON_RIGHT,   0,    0 },
    // ── Joystick axes — toggle on: axis goes to ±JOYSTICK_MAX ────────────
    { 'w',       0,                          0,   85 },
    { 's',       0,                          0,  -85 },
    { 'a',       0,                        -85,    0 },
    { 'd',       0,                         85,    0 },
};
static constexpr int KEY_TABLE_LEN =
    static_cast<int>(sizeof(KEY_TABLE) / sizeof(KEY_TABLE[0]));

// ─── Controller window (left panel) ──────────────────────────────────────────

static constexpr int CTRL_W = 46;

static void draw_controller(WINDOW *w, int /*h*/,
                             uint16_t btns, int8_t sx, int8_t sy,
                             int8_t octave)
{
    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " N64 Controller ");

    // Render a button label: bold green when active, dim when idle
    auto btn = [&](int row, int col, const char *lbl, bool active) {
        if (active) wattron(w, COLOR_PAIR(CP_BTN_ON) | A_BOLD);
        else        wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
        mvwprintw(w, row, col, "%s", lbl);
        if (active) wattroff(w, COLOR_PAIR(CP_BTN_ON) | A_BOLD);
        else        wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);
    };

    mvwprintw(w, 1, 2, "Octave shift: %+d", octave);

    // Row 2: shoulder buttons
    btn(2,  2, "[L]", btns & JOYBUS_N64_BUTTON_L);
    wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
    mvwprintw(w, 2,  6, "(Q)");
    wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);
    btn(2, 36, "[R]", btns & JOYBUS_N64_BUTTON_R);
    wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
    mvwprintw(w, 2, 40, "(E)");
    wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);

    // Rows 3-6: D-pad (left cluster)
    btn(3,  8, "[^]", btns & JOYBUS_N64_BUTTON_UP);
    btn(4,  4, "[<]", btns & JOYBUS_N64_BUTTON_LEFT);
    btn(4, 10, "[>]", btns & JOYBUS_N64_BUTTON_RIGHT);
    btn(5,  8, "[v]", btns & JOYBUS_N64_BUTTON_DOWN);
    wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
    mvwprintw(w, 6, 4, "Arrows");
    wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);

    // Rows 3-6: Joystick (centre)
    {
        mvwprintw(w, 3, 16, ".---.");
        mvwprintw(w, 4, 16, "|   |");
        mvwprintw(w, 5, 16, "'---'");
        // Place indicator dot — one cell of movement each direction
        const int dx = (sx >  10) ?  1 : (sx < -10) ? -1 : 0;
        const int dy = (sy >  10) ? -1 : (sy < -10) ?  1 : 0;
        wattron(w, COLOR_PAIR(CP_BEND) | A_BOLD);
        mvwaddch(w, 4 + dy, 18 + dx, 'o');
        wattroff(w, COLOR_PAIR(CP_BEND) | A_BOLD);
        wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
        mvwprintw(w, 6, 16, "WASD");
        wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);
    }

    // Rows 3-6: C-buttons (right-centre cluster)
    btn(3, 24, "[C^]", btns & JOYBUS_N64_BUTTON_C_UP);
    btn(4, 22, "[C<]", btns & JOYBUS_N64_BUTTON_C_LEFT);
    btn(4, 29, "[C>]", btns & JOYBUS_N64_BUTTON_C_RIGHT);
    btn(5, 24, "[Cv]", btns & JOYBUS_N64_BUTTON_C_DOWN);
    wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
    mvwprintw(w, 3, 29, "(I)");
    mvwprintw(w, 4, 19, "(U)");
    mvwprintw(w, 4, 33, "(O)");
    mvwprintw(w, 5, 29, "(M)");
    wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);

    // Rows 3-5: A / B (far right)
    btn(3, 38, "[A]", btns & JOYBUS_N64_BUTTON_A);
    btn(5, 38, "[B]", btns & JOYBUS_N64_BUTTON_B);
    wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
    mvwprintw(w, 3, 42, "(J)");
    mvwprintw(w, 5, 42, "(K)");
    wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);

    // Rows 7-8: Z and Start
    btn(7,  2, "[Z]", btns & JOYBUS_N64_BUTTON_Z);
    wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
    mvwprintw(w, 7,  6, "(Space)");
    wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);
    btn(7, 17, "[Start]", btns & JOYBUS_N64_BUTTON_START);
    wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
    mvwprintw(w, 7, 25, "(Enter)");
    wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);

    // Stick readout
    mvwprintw(w, 9, 2, "Stick: X=%+4d  Y=%+4d", sx, sy);

    // Key legend
    wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
    mvwprintw(w, 11, 2, "-- Key layout (toggle on/off) ----------");
    mvwprintw(w, 12, 2, " J=A  K=B  I=C^  M=Cv  U=C<  O=C>");
    mvwprintw(w, 13, 2, " Space=Z       Q=L(oct-)  E=R(oct+)");
    mvwprintw(w, 14, 2, " Arrow keys = D-pad");
    mvwprintw(w, 15, 2, " W/A/S/D    = Joystick axes");
    mvwprintw(w, 16, 2, " Enter=Start/Panic   Esc=Quit");
    wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);

    wrefresh(w);
}

// ─── MIDI log window (right panel) ───────────────────────────────────────────

static void draw_log(WINDOW *w, int height)
{
    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " MIDI Events (newest at bottom) ");

    const int inner = height - 2;
    const int n     = static_cast<int>(g_log.size());
    const int start = std::max(0, n - inner);

    for (int i = start, row = 1; i < n && row < height - 1; ++i, ++row) {
        const auto &e = g_log[i];
        if (e.color > 0) wattron(w, COLOR_PAIR(e.color));
        mvwprintw(w, row, 2, "%-46s", e.text.c_str());
        wattron(w, A_DIM);
        wprintw(w, " %s", e.hex.c_str());
        wattroff(w, A_DIM);
        if (e.color > 0) wattroff(w, COLOR_PAIR(e.color));
    }

    wrefresh(w);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char **argv)
{
    // Parse optional --midi-out <path> argument
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--midi-out") == 0) {
            g_midi_out = fopen(argv[i + 1], "wb");
            if (!g_midi_out) {
                fprintf(stderr, "Cannot open MIDI output path: %s\n", argv[i + 1]);
                return 1;
            }
        }
    }

    // ── ncurses init ─────────────────────────────────────────────────────────
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);

    if (has_colors()) {
        start_color();
        init_pair(CP_NOTE_ON,  COLOR_GREEN,   COLOR_BLACK);
        init_pair(CP_NOTE_OFF, COLOR_RED,     COLOR_BLACK);
        init_pair(CP_CC,       COLOR_CYAN,    COLOR_BLACK);
        init_pair(CP_PC,       COLOR_YELLOW,  COLOR_BLACK);
        init_pair(CP_BEND,     COLOR_MAGENTA, COLOR_BLACK);
        init_pair(CP_RT,       COLOR_WHITE,   COLOR_BLACK);
        init_pair(CP_HEADER,   COLOR_WHITE,   COLOR_BLUE);
        init_pair(CP_DIM,      COLOR_WHITE,   COLOR_BLACK);
        init_pair(CP_BTN_ON,   COLOR_GREEN,   COLOR_BLACK);
    }

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int log_w  = cols - CTRL_W;
    int height = rows - 1;

    WINDOW *ctrl_win = newwin(height, CTRL_W, 0, 0);
    WINDOW *log_win  = newwin(height, log_w,  0, CTRL_W);
    WINDOW *stat_win = newwin(1,      cols,   rows - 1, 0);

    // ── Mapper (channel 0, matching the first controller in the firmware) ─────
    Mapping mapper(0);
    midi_init();

    // Toggle set: keys that are currently in the "on" (held) state
    std::set<int> toggled;

    // Mirror the Mapping class's private m_octave_shift for the status bar
    int8_t    octave_disp = 0;
    uint16_t  prev_btns   = 0;

    // ── Main loop ─────────────────────────────────────────────────────────────
    bool running = true;
    while (running) {

        // Drain all pending ncurses key events and flip the toggle set
        int ch;
        while ((ch = getch()) != ERR) {
            if (ch == 27) { running = false; break; }   // ESC → quit
            if (toggled.count(ch)) toggled.erase(ch);
            else                   toggled.insert(ch);
        }
        if (!running) break;

        // Build N64State from the current toggle set
        uint16_t btns    = 0;
        int      sx_acc  = 0;
        int      sy_acc  = 0;
        for (const auto &ke : KEY_TABLE) {
            if (!toggled.count(ke.key)) continue;
            btns   |= ke.btn_mask;
            sx_acc += ke.sx;
            sy_acc += ke.sy;
        }
        const auto sx = static_cast<int8_t>(std::clamp(sx_acc, -85, 85));
        const auto sy = static_cast<int8_t>(std::clamp(sy_acc, -85, 85));

        // Mirror octave shift and panic reset for the display
        if ((btns & JOYBUS_N64_BUTTON_L) && !(prev_btns & JOYBUS_N64_BUTTON_L))
            octave_disp = std::max<int8_t>(octave_disp - 1, -3);
        if ((btns & JOYBUS_N64_BUTTON_R) && !(prev_btns & JOYBUS_N64_BUTTON_R))
            octave_disp = std::min<int8_t>(octave_disp + 1, +3);
        if ((btns & JOYBUS_N64_BUTTON_START) && !(prev_btns & JOYBUS_N64_BUTTON_START))
            octave_disp = 0;
        prev_btns = btns;

        // ── Feed into the real Mapping logic (this is what we are verifying) ──
        N64State state{btns, sx, sy};
        mapper.process(state);

        // ── Redraw UI ─────────────────────────────────────────────────────────

        // Respond to terminal resize
        int new_rows, new_cols;
        getmaxyx(stdscr, new_rows, new_cols);
        if (new_rows != rows || new_cols != cols) {
            rows   = new_rows;
            cols   = new_cols;
            log_w  = cols - CTRL_W;
            height = rows - 1;
            wresize(ctrl_win, height, CTRL_W);
            wresize(log_win,  height, log_w);
            wresize(stat_win, 1,      cols);
            mvwin(log_win,  0,        CTRL_W);
            mvwin(stat_win, rows - 1, 0);
            clearok(stdscr, TRUE);
        }

        draw_controller(ctrl_win, height, btns, sx, sy, octave_disp);
        draw_log(log_win, height);

        // Status bar
        werase(stat_win);
        wattron(stat_win, COLOR_PAIR(CP_HEADER));
        mvwprintw(stat_win, 0, 0,
                  " 64-to-MIDI Tester  |  ch 0  |  oct %+d  "
                  "|  events: %-4zu  |  toggle keys on/off  |  Esc=quit ",
                  octave_disp, g_log.size());
        wattroff(stat_win, COLOR_PAIR(CP_HEADER));
        wrefresh(stat_win);

        // ~60 fps — responsive without burning CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    delwin(ctrl_win);
    delwin(log_win);
    delwin(stat_win);
    endwin();
    if (g_midi_out) fclose(g_midi_out);
    return 0;
}

