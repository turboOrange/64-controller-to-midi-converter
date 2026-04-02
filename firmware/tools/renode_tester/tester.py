#!/usr/bin/env python3
"""
firmware/tools/renode_tester/tester.py

Interactive N64 controller tester backed by a live Renode simulation.

Architecture
────────────
  This script launches Renode as a background process which runs the actual
  ARM firmware binary (n64_midi_converter_sim.elf) inside a virtual RP2040.
  Two TCP sockets bridge the UI to the simulation:

    Monitor socket (port 1234) — Renode's telnet monitor.
      The tester sends "sysbus WriteDoubleWord" commands here to inject
      controller state into the SRAM region read by n64_controller_sim.cpp.

    UART socket (port 4445) — UART1 TX output.
      Every MIDI byte the firmware writes to UART1 arrives here as a raw
      byte.  A background thread reads and decodes them in real time.

  The UI layout is identical to the native mapping tester (tools/n64_midi_tester)
  so you can compare the two side-by-side.

Usage
─────
  Build the simulation ELF first:
      cd firmware
      cmake -B build_sim -G Ninja -DSIMULATION=1
      ninja -C build_sim

  Then run:
      python3 firmware/tools/renode_tester/tester.py

  Optional flags:
      --elf   <path>   Override ELF path (default: firmware/build_sim/…elf)
      --renode <cmd>   Override renode executable (default: renode)
      --monitor-port <n>  Renode monitor port (default: 1234)
      --uart-port    <n>  UART socket port    (default: 4445)
"""

from __future__ import annotations

import argparse
import curses
import shutil
import socket
import subprocess
import sys
import threading
import time
from collections import deque
from pathlib import Path

# ─── Paths ────────────────────────────────────────────────────────────────────

SCRIPT_DIR   = Path(__file__).parent.resolve()
FIRMWARE_DIR = SCRIPT_DIR.parent.parent                      # firmware/
DEFAULT_ELF  = FIRMWARE_DIR / "build_sim" / "n64_midi_converter_sim.elf"
RESC_PATH    = FIRMWARE_DIR / "renode" / "n64_midi_converter.resc"

# ─── N64 button bitmasks (from joybus/n64.h) ─────────────────────────────────

BTN_RIGHT   = 1 << 0
BTN_LEFT    = 1 << 1
BTN_DOWN    = 1 << 2
BTN_UP      = 1 << 3
BTN_START   = 1 << 4
BTN_Z       = 1 << 5
BTN_B       = 1 << 6
BTN_A       = 1 << 7
BTN_C_RIGHT = 1 << 8
BTN_C_LEFT  = 1 << 9
BTN_C_DOWN  = 1 << 10
BTN_C_UP    = 1 << 11
BTN_R       = 1 << 12
BTN_L       = 1 << 13

# SRAM address of SimSlot[0] — must match n64_controller_sim.cpp
SIM_STATE_BASE = 0x20040000

# ─── Key → N64 input table ────────────────────────────────────────────────────
# Built lazily because curses constants are only valid after initscr().

def _build_key_table() -> list[tuple[int, int, int, int]]:
    """Return list of (ncurses_key, btn_mask, stick_x, stick_y)."""
    return [
        # Note / action buttons
        (ord("j"), BTN_A,       0,    0),
        (ord("k"), BTN_B,       0,    0),
        (ord("i"), BTN_C_UP,    0,    0),
        (ord("m"), BTN_C_DOWN,  0,    0),
        (ord("u"), BTN_C_LEFT,  0,    0),
        (ord("o"), BTN_C_RIGHT, 0,    0),
        # Control
        (ord(" "), BTN_Z,       0,    0),
        (ord("\n"),BTN_START,   0,    0),
        (curses.KEY_ENTER, BTN_START, 0, 0),
        (ord("q"), BTN_L,       0,    0),
        (ord("e"), BTN_R,       0,    0),
        # D-pad
        (curses.KEY_UP,    BTN_UP,    0,    0),
        (curses.KEY_DOWN,  BTN_DOWN,  0,    0),
        (curses.KEY_LEFT,  BTN_LEFT,  0,    0),
        (curses.KEY_RIGHT, BTN_RIGHT, 0,    0),
        # Joystick axes — toggle on: axis goes to ±JOYSTICK_MAX
        (ord("w"), 0,  0,   85),
        (ord("s"), 0,  0,  -85),
        (ord("a"), 0, -85,   0),
        (ord("d"), 0,  85,   0),
    ]

# ─── MIDI helpers ─────────────────────────────────────────────────────────────

_NOTE_NAMES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]

def _note_name(n: int) -> str:
    return f"{_NOTE_NAMES[n % 12]}{n // 12 - 1}"


class MidiParser:
    """Stateful single-byte MIDI stream parser."""

    def __init__(self) -> None:
        self._buf: list[int] = []

    def feed(self, byte: int) -> tuple[str, str, str] | None:
        """
        Feed one raw byte.
        Returns (human_text, hex_str, color_key) or None if incomplete.
        color_key is one of: note_on, note_off, cc, pc, bend, rt
        """
        # Single-byte real-time messages (0xF8–0xFF)
        if byte >= 0xF8:
            names = {0xF8: "Clock", 0xFA: "Start", 0xFB: "Continue", 0xFC: "Stop"}
            return (f"REALTIME  {names.get(byte, 'RT')}", f"{byte:02X}", "rt")

        if byte & 0x80:          # Any other status byte — start a new message
            self._buf = [byte]
            return None

        self._buf.append(byte)
        if not self._buf:
            return None

        st  = self._buf[0]
        typ = st & 0xF0
        ch  = st & 0x0F

        if typ == 0x80 and len(self._buf) == 3:
            n, v = self._buf[1], self._buf[2]
            self._buf = [st]
            return (f"NOTE OFF  ch{ch:<2} {_note_name(n):<4} ({n:3})",
                    f"{st:02X} {n:02X} {v:02X}", "note_off")

        if typ == 0x90 and len(self._buf) == 3:
            n, v = self._buf[1], self._buf[2]
            self._buf = [st]
            if v == 0:
                return (f"NOTE OFF  ch{ch:<2} {_note_name(n):<4} ({n:3})  vel 0",
                        f"{st:02X} {n:02X} {v:02X}", "note_off")
            return (f"NOTE ON   ch{ch:<2} {_note_name(n):<4} ({n:3})  vel {v}",
                    f"{st:02X} {n:02X} {v:02X}", "note_on")

        if typ == 0xB0 and len(self._buf) == 3:
            cc, v = self._buf[1], self._buf[2]
            label = {1: "Mod Wheel", 64: "Sustain", 123: "AllNotesOff"}.get(cc, "CC")
            self._buf = [st]
            color = "note_off" if cc == 123 else "cc"
            return (f"CC        ch{ch:<2} #{cc:<3} {label:<13}  val {v}",
                    f"{st:02X} {cc:02X} {v:02X}", color)

        if typ == 0xC0 and len(self._buf) == 2:
            prog = self._buf[1]
            self._buf = [st]
            return (f"PROG CHG  ch{ch:<2} #{prog}  (GM {prog + 1})",
                    f"{st:02X} {prog:02X}", "pc")

        if typ == 0xE0 and len(self._buf) == 3:
            lsb, msb = self._buf[1], self._buf[2]
            bend = ((msb << 7) | lsb) - 8192
            self._buf = [st]
            if bend == 0:
                return None          # suppress zero-bend log noise
            return (f"PITCH BND ch{ch:<2} {bend:+d}",
                    f"{st:02X} {lsb:02X} {msb:02X}", "bend")

        return None

# ─── Renode binary discovery ─────────────────────────────────────────────────

def find_renode(hint: str) -> str:
    """
    Locate the renode executable.

    Search order:
      1. The value of --renode if it is not the default 'renode'.
      2. PATH  (works when inside `nix develop` or renode is system-installed).
      3. Nix store via `nix eval --raw nixpkgs#renode`  (works even outside
         the dev shell, as long as nix is available and nixpkgs is on the
         flake registry).
      4. Direct glob of /nix/store/*renode*/bin/renode  (offline fallback).

    Exits with a helpful message if renode cannot be found.
    """
    # Explicit override
    if hint != "renode":
        p = shutil.which(hint) or hint
        if Path(p).exists():
            return p
        print(f"ERROR: renode executable not found at '{hint}'", file=sys.stderr)
        sys.exit(1)

    # PATH
    found = shutil.which("renode")
    if found:
        return found

    # Nix eval fallback (queries the Nix store without rebuilding)
    try:
        res = subprocess.run(
            ["nix", "eval", "--raw", "nixpkgs#renode"],
            capture_output=True, text=True, timeout=15,
        )
        if res.returncode == 0:
            candidate = Path(res.stdout.strip()) / "bin" / "renode"
            if candidate.exists():
                print(f"renode found via nix eval: {candidate}", file=sys.stderr)
                return str(candidate)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass

    # Glob /nix/store directly (offline, fast)
    for p in sorted(Path("/nix/store").glob("*renode*/bin/renode")):
        if p.is_file():
            print(f"renode found in Nix store: {p}", file=sys.stderr)
            return str(p)

    print("ERROR: 'renode' not found.", file=sys.stderr)
    print("  Option 1 — enter the project dev shell first:", file=sys.stderr)
    print("      nix develop", file=sys.stderr)
    print("  Option 2 — pass the binary path explicitly:", file=sys.stderr)
    print("      python3 tester.py --renode /path/to/renode", file=sys.stderr)
    sys.exit(1)




class RenodeMonitor:
    """
    Thin wrapper around Renode's telnet monitor socket.

    Commands are sent as plain text lines.  Responses are drained after a
    short delay to prevent the socket's receive buffer from filling up.
    The caller never needs to inspect the response.
    """

    def __init__(self, port: int, timeout_s: float = 60.0) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        deadline = time.monotonic() + timeout_s
        while True:
            try:
                self._sock.connect(("127.0.0.1", port))
                break
            except (ConnectionRefusedError, OSError):
                if time.monotonic() > deadline:
                    raise TimeoutError(
                        f"Renode monitor did not become available on port {port} "
                        f"within {timeout_s:.0f} s"
                    )
                time.sleep(0.2)
        self._sock.settimeout(0.3)
        self._drain()      # consume the initial Renode banner + first prompt

    # ------------------------------------------------------------------

    def _drain(self) -> None:
        try:
            while self._sock.recv(4096):
                pass
        except (socket.timeout, OSError):
            pass

    def cmd(self, command: str) -> None:
        """Fire a monitor command and discard the response."""
        try:
            self._sock.sendall((command + "\n").encode())
            time.sleep(0.04)
            self._drain()
        except OSError:
            pass

    def write_state(self, buttons: int, stick_x: int, stick_y: int,
                    ctrl_idx: int = 0) -> None:
        """
        Write one SimSlot into Renode's simulated SRAM with a single
        32-bit store.  Memory layout (little-endian, matches SimSlot):
            byte 0-1 : buttons (uint16_t)
            byte 2   : stick_x (int8_t cast to uint8)
            byte 3   : stick_y (int8_t cast to uint8)
        """
        addr  = SIM_STATE_BASE + ctrl_idx * 4
        value = (
            (buttons & 0xFFFF)        |
            ((stick_x & 0xFF) << 16)  |
            ((stick_y & 0xFF) << 24)
        )
        self.cmd(f"sysbus WriteDoubleWord 0x{addr:08X} 0x{value:08X}")

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass

# ─── Shared MIDI event log ────────────────────────────────────────────────────

# Each entry: (human_text, hex_str, color_key)
g_log: deque[tuple[str, str, str]] = deque(maxlen=500)
g_log_lock = threading.Lock()


def _log_push(text: str, hex_str: str, color: str) -> None:
    with g_log_lock:
        g_log.append((text, hex_str, color))

# ─── UART reader thread ───────────────────────────────────────────────────────

def uart_reader(uart_port: int, stop: threading.Event) -> None:
    """
    Background thread: connect to Renode's UART socket terminal, read raw
    MIDI bytes, parse them, and push decoded events into g_log.
    Reconnects automatically if the connection drops.
    """
    parser = MidiParser()
    sock: socket.socket | None = None

    while not stop.is_set():
        if sock is None:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                sock.settimeout(1.0)
                sock.connect(("127.0.0.1", uart_port))
            except (ConnectionRefusedError, OSError, socket.timeout):
                if sock:
                    sock.close()
                sock = None
                time.sleep(0.5)
                continue

        try:
            data = sock.recv(256)
            if not data:
                sock.close()
                sock = None
                continue
            for b in data:
                result = parser.feed(b)
                if result:
                    _log_push(*result)
        except socket.timeout:
            continue
        except OSError:
            if sock:
                sock.close()
            sock = None

    if sock:
        sock.close()

# ─── ncurses UI ───────────────────────────────────────────────────────────────

CTRL_W = 46     # width of the left controller panel

# Color-pair indices — must match _init_colors()
_CP: dict[str, int] = {
    "note_on":  1,
    "note_off": 2,
    "cc":       3,
    "pc":       4,
    "bend":     5,
    "rt":       6,
    "header":   7,
    "dim":      8,
    "btn_on":   9,
}


def _init_colors() -> None:
    curses.start_color()
    curses.init_pair(_CP["note_on"],  curses.COLOR_GREEN,   curses.COLOR_BLACK)
    curses.init_pair(_CP["note_off"], curses.COLOR_RED,     curses.COLOR_BLACK)
    curses.init_pair(_CP["cc"],       curses.COLOR_CYAN,    curses.COLOR_BLACK)
    curses.init_pair(_CP["pc"],       curses.COLOR_YELLOW,  curses.COLOR_BLACK)
    curses.init_pair(_CP["bend"],     curses.COLOR_MAGENTA, curses.COLOR_BLACK)
    curses.init_pair(_CP["rt"],       curses.COLOR_WHITE,   curses.COLOR_BLACK)
    curses.init_pair(_CP["header"],   curses.COLOR_WHITE,   curses.COLOR_BLUE)
    curses.init_pair(_CP["dim"],      curses.COLOR_WHITE,   curses.COLOR_BLACK)
    curses.init_pair(_CP["btn_on"],   curses.COLOR_GREEN,   curses.COLOR_BLACK)


def _btn(win: "curses._CursesWindow", row: int, col: int,
         lbl: str, active: bool) -> None:
    """Render a button label: bold green when active, dim when idle."""
    if active:
        win.attron(curses.color_pair(_CP["btn_on"]) | curses.A_BOLD)
    else:
        win.attron(curses.color_pair(_CP["dim"]) | curses.A_DIM)
    try:
        win.addstr(row, col, lbl)
    except curses.error:
        pass
    if active:
        win.attroff(curses.color_pair(_CP["btn_on"]) | curses.A_BOLD)
    else:
        win.attroff(curses.color_pair(_CP["dim"]) | curses.A_DIM)


def _dim(win: "curses._CursesWindow", row: int, col: int, text: str) -> None:
    win.attron(curses.color_pair(_CP["dim"]) | curses.A_DIM)
    try:
        win.addstr(row, col, text)
    except curses.error:
        pass
    win.attroff(curses.color_pair(_CP["dim"]) | curses.A_DIM)


def _draw_controller(win: "curses._CursesWindow",
                     btns: int, sx: int, sy: int, octave: int) -> None:
    win.erase()
    win.box()
    try:
        win.addstr(0, 2, " N64 Controller  [Renode] ")
        win.addstr(1, 2, f"Octave shift: {octave:+d}")
    except curses.error:
        pass

    # Shoulder buttons
    _btn(win, 2,  2, "[L]", bool(btns & BTN_L));  _dim(win, 2,  6, "(Q)")
    _btn(win, 2, 36, "[R]", bool(btns & BTN_R));  _dim(win, 2, 40, "(E)")

    # D-pad
    _btn(win, 3,  8, "[^]", bool(btns & BTN_UP))
    _btn(win, 4,  4, "[<]", bool(btns & BTN_LEFT))
    _btn(win, 4, 10, "[>]", bool(btns & BTN_RIGHT))
    _btn(win, 5,  8, "[v]", bool(btns & BTN_DOWN))
    _dim(win, 6,  4, "Arrows")

    # Joystick
    for r, c, s in [(3, 16, ".---."), (4, 16, "|   |"), (5, 16, "'---'")]:
        try:
            win.addstr(r, c, s)
        except curses.error:
            pass
    dx = 1 if sx > 10 else (-1 if sx < -10 else 0)
    dy = -1 if sy > 10 else (1 if sy < -10 else 0)
    win.attron(curses.color_pair(_CP["bend"]) | curses.A_BOLD)
    try:
        win.addch(4 + dy, 18 + dx, ord("o"))
    except curses.error:
        pass
    win.attroff(curses.color_pair(_CP["bend"]) | curses.A_BOLD)
    _dim(win, 6, 16, "WASD")

    # C-buttons
    _btn(win, 3, 24, "[C^]", bool(btns & BTN_C_UP));   _dim(win, 3, 29, "(I)")
    _btn(win, 4, 22, "[C<]", bool(btns & BTN_C_LEFT));  _dim(win, 4, 19, "(U)")
    _btn(win, 4, 29, "[C>]", bool(btns & BTN_C_RIGHT)); _dim(win, 4, 33, "(O)")
    _btn(win, 5, 24, "[Cv]", bool(btns & BTN_C_DOWN));  _dim(win, 5, 29, "(M)")

    # A / B
    _btn(win, 3, 38, "[A]", bool(btns & BTN_A)); _dim(win, 3, 42, "(J)")
    _btn(win, 5, 38, "[B]", bool(btns & BTN_B)); _dim(win, 5, 42, "(K)")

    # Z / Start
    _btn(win, 7,  2, "[Z]",     bool(btns & BTN_Z));     _dim(win, 7,  6, "(Space)")
    _btn(win, 7, 17, "[Start]", bool(btns & BTN_START));  _dim(win, 7, 25, "(Enter)")

    # Stick readout
    try:
        win.addstr(9, 2, f"Stick: X={sx:+4d}  Y={sy:+4d}")
    except curses.error:
        pass

    # Key legend
    for row, text in enumerate([
        "-- Key layout (toggle on/off) ----------",
        " J=A  K=B  I=C^  M=Cv  U=C<  O=C>",
        " Space=Z       Q=L(oct-)  E=R(oct+)",
        " Arrow keys = D-pad",
        " W/A/S/D    = Joystick axes",
        " Enter=Start/Panic   Esc=Quit",
    ], start=11):
        _dim(win, row, 2, text)

    win.noutrefresh()


def _draw_log(win: "curses._CursesWindow", height: int) -> None:
    win.erase()
    win.box()
    try:
        win.addstr(0, 2, " MIDI Events  (real firmware output via Renode) ")
    except curses.error:
        pass

    inner = height - 2
    with g_log_lock:
        entries = list(g_log)
    start = max(0, len(entries) - inner)

    for row, (text, hex_str, color_key) in enumerate(entries[start:], start=1):
        if row >= height - 1:
            break
        cp = curses.color_pair(_CP.get(color_key, 0))
        win.attron(cp)
        try:
            win.addstr(row, 2, f"{text:<46}")
        except curses.error:
            pass
        win.attroff(cp)
        win.attron(curses.A_DIM)
        try:
            win.addstr(row, 49, hex_str)
        except curses.error:
            pass
        win.attroff(curses.A_DIM)

    win.noutrefresh()

# ─── TUI main loop ────────────────────────────────────────────────────────────

def _tui_main(stdscr: "curses._CursesWindow",
              monitor: RenodeMonitor, uart_port: int) -> None:
    curses.cbreak()
    curses.noecho()
    stdscr.keypad(True)
    stdscr.nodelay(True)
    curses.curs_set(0)
    try:
        curses.set_escdelay(25)
    except AttributeError:
        pass

    if curses.has_colors():
        _init_colors()

    KEY_TABLE = _build_key_table()

    rows, cols = stdscr.getmaxyx()
    log_w  = max(cols - CTRL_W, 20)
    height = rows - 1

    ctrl_win = curses.newwin(height, CTRL_W, 0, 0)
    log_win  = curses.newwin(height, log_w,  0, CTRL_W)
    stat_win = curses.newwin(1, cols, rows - 1, 0)

    toggled:   set[int] = set()
    octave:    int = 0
    prev_btns: int = 0

    # Start the UART reader thread now that the TUI owns the terminal
    stop_evt = threading.Event()
    reader = threading.Thread(target=uart_reader, args=(uart_port, stop_evt),
                              daemon=True)
    reader.start()

    # Tell Renode to begin executing — we're ready to receive UART bytes
    monitor.cmd("start")
    _log_push("SIM STARTED  Renode simulation running…", "", "pc")

    running = True
    while running:
        # ── Input ──────────────────────────────────────────────────────────────
        ch = stdscr.getch()
        while ch != curses.ERR:
            if ch == 27:        # ESC
                running = False
                break
            if ch in toggled:
                toggled.discard(ch)
            else:
                toggled.add(ch)
            ch = stdscr.getch()
        if not running:
            break

        # ── Build N64State from toggle set ────────────────────────────────────
        btns   = 0
        sx_acc = 0
        sy_acc = 0
        for key, mask, ksx, ksy in KEY_TABLE:
            if key in toggled:
                btns   |= mask
                sx_acc += ksx
                sy_acc += ksy
        sx = max(-85, min(85, sx_acc))
        sy = max(-85, min(85, sy_acc))

        # ── Mirror octave shift for display ───────────────────────────────────
        if (btns & BTN_L) and not (prev_btns & BTN_L):
            octave = max(octave - 1, -3)
        if (btns & BTN_R) and not (prev_btns & BTN_R):
            octave = min(octave + 1, +3)
        if (btns & BTN_START) and not (prev_btns & BTN_START):
            octave = 0
        prev_btns = btns

        # ── Inject state into the running firmware via Renode ─────────────────
        monitor.write_state(btns, sx, sy)

        # ── Resize handling ───────────────────────────────────────────────────
        nr, nc = stdscr.getmaxyx()
        if nr != rows or nc != cols:
            rows, cols = nr, nc
            log_w  = max(cols - CTRL_W, 20)
            height = rows - 1
            ctrl_win.resize(height, CTRL_W)
            log_win.resize(height, log_w)
            log_win.mvwin(0, CTRL_W)
            stat_win.resize(1, cols)
            stat_win.mvwin(rows - 1, 0)
            stdscr.clearok(True)

        # ── Redraw ────────────────────────────────────────────────────────────
        _draw_controller(ctrl_win, btns, sx, sy, octave)
        _draw_log(log_win, height)

        stat_win.erase()
        stat_win.attron(curses.color_pair(_CP["header"]))
        with g_log_lock:
            n_ev = len(g_log)
        try:
            stat_win.addstr(
                0, 0,
                f" Renode Tester  |  ch 0  |  oct {octave:+d}  "
                f"|  events: {n_ev:<4}  |  toggle keys  |  Esc=quit "
            )
        except curses.error:
            pass
        stat_win.attroff(curses.color_pair(_CP["header"]))
        stat_win.noutrefresh()

        curses.doupdate()
        time.sleep(0.016)   # ~60 fps

    stop_evt.set()

# ─── Entry point ──────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Renode-backed interactive N64 MIDI converter tester"
    )
    parser.add_argument("--elf",          default=str(DEFAULT_ELF),
                        help="Path to simulation ELF (default: firmware/build_sim/…)")
    parser.add_argument("--renode",       default="renode",
                        help="Renode executable name or path (default: renode)")
    parser.add_argument("--monitor-port", type=int, default=1234,
                        help="Renode monitor port (default: 1234)")
    parser.add_argument("--uart-port",    type=int, default=4445,
                        help="UART socket port (default: 4445)")
    args = parser.parse_args()

    elf_path = Path(args.elf)
    if not elf_path.exists():
        print(f"ERROR: ELF not found: {elf_path}", file=sys.stderr)
        print("Build it first:", file=sys.stderr)
        print("  cd firmware", file=sys.stderr)
        print("  cmake -B build_sim -G Ninja -DSIMULATION=1", file=sys.stderr)
        print("  ninja -C build_sim", file=sys.stderr)
        sys.exit(1)

    if not RESC_PATH.exists():
        print(f"ERROR: Renode script not found: {RESC_PATH}", file=sys.stderr)
        sys.exit(1)

    # ── Launch Renode ─────────────────────────────────────────────────────────
    renode_bin = find_renode(args.renode)
    print(f"Launching Renode ({Path(renode_bin).name}) with {elf_path.name} …")
    renode_proc = subprocess.Popen(
        [renode_bin,
         "--port",        str(args.monitor_port),
         "--disable-xwt",
         str(RESC_PATH)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    monitor: RenodeMonitor | None = None
    try:
        # ── Connect to Renode monitor ─────────────────────────────────────────
        print(f"Waiting for Renode monitor on port {args.monitor_port} …")
        monitor = RenodeMonitor(port=args.monitor_port, timeout_s=90.0)
        print("Monitor ready.")

        # ── Run the ncurses TUI (starts UART reader + issues 'start') ─────────
        curses.wrapper(_tui_main, monitor, args.uart_port)

    except TimeoutError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
    except KeyboardInterrupt:
        pass
    finally:
        if monitor:
            monitor.close()
        renode_proc.terminate()
        try:
            renode_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            renode_proc.kill()


if __name__ == "__main__":
    main()

