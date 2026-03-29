#!/usr/bin/env python3
"""
Razer Chroma SDK Emulator -> Ducky One 2 SF HID Bridge

Emulates the Razer Chroma REST API that games (like Cyberpunk 2077) use,
and forwards per-key RGB data to the Ducky One 2 SF keyboard via raw HID.

Requirements:
    pip install flask hidapi

Usage:
    python chroma_bridge.py

The script will:
1. Start a REST API server on port 54235 (Razer Chroma SDK default)
2. Connect to the Ducky One 2 SF keyboard via raw HID
3. Translate Razer keyboard grid colors to Ducky LED indices
4. Forward colors to the keyboard in real-time
"""

import sys
import time
import json
import uuid
import threading
import logging
from flask import Flask, request, jsonify

try:
    import hid
except ImportError:
    print("ERROR: 'hid' package not found. Install with: pip install hidapi")
    sys.exit(1)

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger("chroma_bridge")

# --- Ducky One 2 SF HID Config ---
DUCKY_VID = 0x445B
DUCKY_PID = 0x07AF
RAW_USAGE_PAGE = 0xFF60
RAW_USAGE_ID = 0x61
RAW_EPSIZE = 32

# --- Chroma HID Protocol ---
CMD_SET_LEDS  = 0x01
CMD_ENABLE    = 0x02
CMD_DISABLE   = 0x03
CMD_HEARTBEAT = 0x04
MAX_LEDS_PER_PACKET = 9

# --- LED count ---
DUCKY_LED_COUNT = 69

# Mapping from Razer Huntsman 65% keyboard grid (row, col) to Ducky One 2 SF
# LED indices. The Ducky has 69 LEDs in the rgb_matrix layout (keyboard.json).
# LED index matches the order in keyboard.json rgb_matrix.layout[].
#
# Razer grid is 6 rows x 22 cols. We map the relevant positions.
# -1 means no corresponding LED on the Ducky.
#
# Ducky LED index layout (from keyboard.json):
#  Row 0: Esc(0) 1(1) 2(2) 3(3) 4(4) 5(5) 6(6) 7(7) 8(8) 9(9) 0(10) -(11) =(12) Bksp(13) Del(14)
#  Row 1: Tab(15) Q(16) W(17) E(18) R(19) T(20) Y(21) U(22) I(23) O(24) P(25) [(26) ](27) \(28) PgUp(29)
#  Row 2: Caps(30) A(31) S(32) D(33) F(34) G(35) H(36) J(37) K(38) L(39) ;(40) '(41) Enter(42) PgDn(43)
#  Row 3: Shift(44) Z(45) X(46) C(47) V(48) B(49) N(50) M(51) ,(52) .(53) /(54) RShift(55) Up(56)
#  Row 4: Ctrl(57) Alt(58) GUI(59) Space(60) [skip] Alt(61) [skip] Fn(62) Ctrl(63) Left(64) Down(65) Right(66) [extra: 67, 68]

# Razer Huntsman 65% uses a 6x22 grid but only certain cells are populated.
# This is a best-effort mapping. Razer row 0 is typically media/function strip.
# Row 1 = number row, Row 2 = QWERTY row, etc.
# Columns are roughly: 0=far left, progressing right.

RAZER_TO_DUCKY = {}

# Row 1 (Razer): Number row - Esc, 1-0, -, =, Backspace, Del
_razer_row1 = [
    (1, 1, 0),   # Esc
    (1, 2, 1),   # 1
    (1, 3, 2),   # 2
    (1, 4, 3),   # 3
    (1, 5, 4),   # 4
    (1, 6, 5),   # 5
    (1, 7, 6),   # 6
    (1, 8, 7),   # 7
    (1, 9, 8),   # 8
    (1, 10, 9),  # 9
    (1, 11, 10), # 0
    (1, 12, 11), # -
    (1, 13, 12), # =
    (1, 14, 13), # Backspace
    (1, 15, 14), # Del
]

# Row 2 (Razer): Tab row
_razer_row2 = [
    (2, 1, 15),  # Tab
    (2, 2, 16),  # Q
    (2, 3, 17),  # W
    (2, 4, 18),  # E
    (2, 5, 19),  # R
    (2, 6, 20),  # T
    (2, 7, 21),  # Y
    (2, 8, 22),  # U
    (2, 9, 23),  # I
    (2, 10, 24), # O
    (2, 11, 25), # P
    (2, 12, 26), # [
    (2, 13, 27), # ]
    (2, 14, 28), # Backslash
    (2, 15, 29), # PgUp
]

# Row 3 (Razer): Home row
_razer_row3 = [
    (3, 1, 30),  # Caps/Fn
    (3, 2, 31),  # A
    (3, 3, 32),  # S
    (3, 4, 33),  # D
    (3, 5, 34),  # F
    (3, 6, 35),  # G
    (3, 7, 36),  # H
    (3, 8, 37),  # J
    (3, 9, 38),  # K
    (3, 10, 39), # L
    (3, 11, 40), # ;
    (3, 12, 41), # '
    (3, 14, 42), # Enter
    (3, 15, 43), # PgDn
]

# Row 4 (Razer): Shift row
_razer_row4 = [
    (4, 1, 44),  # LShift
    (4, 3, 45),  # Z
    (4, 4, 46),  # X
    (4, 5, 47),  # C
    (4, 6, 48),  # V
    (4, 7, 49),  # B
    (4, 8, 50),  # N
    (4, 9, 51),  # M
    (4, 10, 52), # ,
    (4, 11, 53), # .
    (4, 12, 54), # /
    (4, 13, 55), # RShift
    (4, 14, 56), # Up
]

# Row 5 (Razer): Bottom row
_razer_row5 = [
    (5, 1, 57),  # LCtrl
    (5, 2, 58),  # LAlt / LWin
    (5, 3, 59),  # LGUI / LAlt
    (5, 7, 60),  # Space
    (5, 11, 61), # RAlt / RWin
    (5, 12, 62), # Fn
    (5, 13, 63), # RCtrl
    (5, 14, 64), # Left
    (5, 15, 65), # Down
    (5, 16, 66), # Right
]

for _entries in [_razer_row1, _razer_row2, _razer_row3, _razer_row4, _razer_row5]:
    for rrow, rcol, ducky_idx in _entries:
        RAZER_TO_DUCKY[(rrow, rcol)] = ducky_idx


class DuckyHID:
    """Manages raw HID communication with the Ducky One 2 SF."""

    def __init__(self):
        self.device = None
        self._lock = threading.Lock()

    def connect(self):
        """Find and open the Ducky raw HID interface."""
        device_interfaces = hid.enumerate(DUCKY_VID, DUCKY_PID)
        raw_interfaces = [
            i for i in device_interfaces
            if i.get("usage_page") == RAW_USAGE_PAGE and i.get("usage") == RAW_USAGE_ID
        ]

        if not raw_interfaces:
            log.warning("Ducky One 2 SF raw HID interface not found (VID=0x%04X PID=0x%04X)", DUCKY_VID, DUCKY_PID)
            log.info("Available devices:")
            for d in device_interfaces:
                log.info("  path=%s usage_page=0x%04X usage=0x%02X",
                         d.get("path", b"").decode(errors="replace"),
                         d.get("usage_page", 0), d.get("usage", 0))
            return False

        self.device = hid.device()
        self.device.open_path(raw_interfaces[0]["path"])
        log.info("Connected: %s %s", self.device.get_manufacturer_string(), self.device.get_product_string())
        return True

    def disconnect(self):
        if self.device:
            self.device.close()
            self.device = None

    def _send(self, data):
        """Send a 32-byte raw HID report. Returns response or None."""
        if not self.device:
            return None
        # Prepend report ID (0x00)
        report = [0x00] + [0x00] * RAW_EPSIZE
        for i, b in enumerate(data[:RAW_EPSIZE]):
            report[i + 1] = b
        with self._lock:
            try:
                self.device.write(bytes(report))
                return self.device.read(RAW_EPSIZE, 100)
            except Exception as e:
                log.error("HID error: %s", e)
                return None

    def enable_chroma(self):
        return self._send([CMD_ENABLE])

    def disable_chroma(self):
        return self._send([CMD_DISABLE])

    def heartbeat(self):
        return self._send([CMD_HEARTBEAT])

    def set_leds(self, led_colors):
        """Send per-key colors to the keyboard.

        Args:
            led_colors: dict mapping LED index -> (R, G, B)
        """
        if not led_colors:
            return

        # Sort by index and send in batches of MAX_LEDS_PER_PACKET
        sorted_indices = sorted(led_colors.keys())

        # Group consecutive indices for efficient batching
        batch_start = sorted_indices[0]
        batch = []

        for idx in sorted_indices:
            if idx != batch_start + len(batch):
                # Non-consecutive, flush current batch
                self._send_batch(batch_start, batch)
                batch_start = idx
                batch = []
            batch.append(led_colors[idx])
            if len(batch) >= MAX_LEDS_PER_PACKET:
                self._send_batch(batch_start, batch)
                batch_start = idx + 1
                batch = []

        if batch:
            self._send_batch(batch_start, batch)

    def _send_batch(self, start_idx, colors):
        """Send a batch of LED colors starting at start_idx."""
        data = [CMD_SET_LEDS, start_idx, len(colors)]
        for r, g, b in colors:
            data.extend([r, g, b])
        self._send(data)

    def set_all_leds(self, led_array):
        """Send all 69 LED colors at once (full frame).

        Args:
            led_array: list of 69 (R, G, B) tuples
        """
        for start in range(0, len(led_array), MAX_LEDS_PER_PACKET):
            end = min(start + MAX_LEDS_PER_PACKET, len(led_array))
            batch = led_array[start:end]
            data = [CMD_SET_LEDS, start, len(batch)]
            for r, g, b in batch:
                data.extend([r, g, b])
            self._send(data)


# --- Global state ---
ducky = DuckyHID()
app = Flask(__name__)
app.logger.setLevel(logging.WARNING)

# Chroma SDK session tracking
sessions = {}  # session_id -> {"effects": {}, "created": timestamp}

# Heartbeat thread
heartbeat_running = False


def heartbeat_loop():
    """Periodically send heartbeats to keep Chroma mode alive."""
    while heartbeat_running:
        ducky.heartbeat()
        time.sleep(2)


def start_heartbeat():
    global heartbeat_running
    heartbeat_running = True
    t = threading.Thread(target=heartbeat_loop, daemon=True)
    t.start()


def stop_heartbeat():
    global heartbeat_running
    heartbeat_running = False


def razer_grid_to_ducky(rows):
    """Convert Razer Chroma keyboard grid to Ducky LED colors.

    Args:
        rows: list of rows, each row is a list of 0xBBGGRR color values
              (Razer uses BGR format)

    Returns:
        list of 69 (R, G, B) tuples
    """
    led_array = [(0, 0, 0)] * DUCKY_LED_COUNT

    for row_idx, row in enumerate(rows):
        for col_idx, color in enumerate(row):
            ducky_idx = RAZER_TO_DUCKY.get((row_idx, col_idx))
            if ducky_idx is not None and ducky_idx < DUCKY_LED_COUNT:
                # Razer color is 0x00BBGGRR
                b = (color >> 16) & 0xFF
                g = (color >> 8) & 0xFF
                r = color & 0xFF
                led_array[ducky_idx] = (r, g, b)

    return led_array


# --- Razer Chroma REST API Emulation ---
# Games connect to http://localhost:54235/razer/chromasdk

@app.route("/razer/chromasdk", methods=["POST"])
def chromasdk_init():
    """Initialize a Chroma SDK session."""
    data = request.get_json(silent=True) or {}
    session_id = str(uuid.uuid4())
    sessions[session_id] = {
        "created": time.time(),
        "effects": {},
    }

    log.info("Chroma SDK session created: %s (app: %s)", session_id, data.get("title", "unknown"))

    return jsonify({
        "sessionid": 0,
        "uri": f"http://localhost:54235/chromasdk/{session_id}",
    })


@app.route("/chromasdk/<session_id>", methods=["DELETE"])
def chromasdk_uninit(session_id):
    """Uninitialize a Chroma SDK session."""
    sessions.pop(session_id, None)
    log.info("Chroma SDK session deleted: %s", session_id)
    return jsonify({"result": 0})


@app.route("/chromasdk/<session_id>/heartbeat", methods=["PUT"])
def chromasdk_heartbeat(session_id):
    """Keep the session alive."""
    if session_id in sessions:
        sessions[session_id]["created"] = time.time()
    return jsonify({"tick": int(time.time())})


@app.route("/chromasdk/<session_id>/keyboard", methods=["PUT", "POST"])
def chromasdk_keyboard(session_id):
    """Receive keyboard effect from game."""
    data = request.get_json(silent=True) or {}
    effect = data.get("effect")
    param = data.get("param")

    if effect == "CHROMA_CUSTOM" and param:
        # param is a 6x22 grid of BGR color values
        led_array = razer_grid_to_ducky(param)
        ducky.set_all_leds(led_array)
    elif effect == "CHROMA_CUSTOM_KEY" and param:
        # param has "color" and "key" arrays
        color_grid = param.get("color", [])
        if color_grid:
            led_array = razer_grid_to_ducky(color_grid)
            ducky.set_all_leds(led_array)
    elif effect == "CHROMA_STATIC" and param:
        color = param.get("color", 0)
        b = (color >> 16) & 0xFF
        g = (color >> 8) & 0xFF
        r = color & 0xFF
        led_array = [(r, g, b)] * DUCKY_LED_COUNT
        ducky.set_all_leds(led_array)
    elif effect == "CHROMA_NONE":
        led_array = [(0, 0, 0)] * DUCKY_LED_COUNT
        ducky.set_all_leds(led_array)

    effect_id = str(uuid.uuid4())
    return jsonify({"result": 0, "id": effect_id})


@app.route("/chromasdk/<session_id>/chromalink", methods=["PUT", "POST"])
@app.route("/chromasdk/<session_id>/headset", methods=["PUT", "POST"])
@app.route("/chromasdk/<session_id>/mouse", methods=["PUT", "POST"])
@app.route("/chromasdk/<session_id>/mousepad", methods=["PUT", "POST"])
@app.route("/chromasdk/<session_id>/keypad", methods=["PUT", "POST"])
def chromasdk_other_device(session_id):
    """Accept but ignore effects for non-keyboard devices."""
    return jsonify({"result": 0, "id": str(uuid.uuid4())})


@app.route("/chromasdk/<session_id>/createeffect", methods=["POST"])
def chromasdk_create_effect(session_id):
    """Create a named effect (for batch operations)."""
    data = request.get_json(silent=True) or {}
    effect_id = str(uuid.uuid4())

    if session_id in sessions:
        sessions[session_id]["effects"][effect_id] = data

    return jsonify({"result": 0, "id": effect_id})


@app.route("/chromasdk/<session_id>/effect", methods=["PUT"])
def chromasdk_set_effect(session_id):
    """Apply a previously created effect by ID."""
    data = request.get_json(silent=True) or {}
    effect_id = data.get("id")

    if session_id in sessions and effect_id in sessions[session_id].get("effects", {}):
        effect_data = sessions[session_id]["effects"][effect_id]
        # Re-process as if it were a keyboard effect
        effect = effect_data.get("effect")
        param = effect_data.get("param")
        if effect == "CHROMA_CUSTOM" and param:
            led_array = razer_grid_to_ducky(param)
            ducky.set_all_leds(led_array)

    return jsonify({"result": 0})


def main():
    log.info("Ducky One 2 SF Chroma Bridge")
    log.info("VID=0x%04X PID=0x%04X", DUCKY_VID, DUCKY_PID)

    if not ducky.connect():
        log.error("Failed to connect to keyboard. Is it plugged in?")
        log.error("Make sure the firmware has RAW_ENABLE=yes and is flashed.")
        sys.exit(1)

    ducky.enable_chroma()
    start_heartbeat()

    log.info("Starting Chroma SDK emulator on http://localhost:54235")
    log.info("Games should auto-detect Chroma support.")

    try:
        app.run(host="127.0.0.1", port=54235, threaded=True)
    except KeyboardInterrupt:
        pass
    finally:
        log.info("Shutting down...")
        stop_heartbeat()
        ducky.disable_chroma()
        ducky.disconnect()


if __name__ == "__main__":
    main()
