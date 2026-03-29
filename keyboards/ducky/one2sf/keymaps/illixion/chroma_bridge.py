#!/usr/bin/env python3
"""
Aurora RGB -> Ducky One 2 SF HID Bridge

Reads per-key RGB data from Aurora's DeviceLedMap shared memory and forwards
it to the Ducky One 2 SF keyboard via raw HID.

Why Aurora? Games like Cyberpunk 2077 use the native Razer Chroma SDK DLL
(RzChromaSDK64.dll), NOT a REST API. Aurora intercepts those native DLL calls
and makes the per-key color data available in a Windows shared memory region
that this script reads from.

Requirements:
    pip install hidapi

Setup:
    1. Install Aurora RGB (https://www.project-aurora.com/)
    2. In Aurora settings, install Razer Chroma SDK
    3. Ensure "Razer Chroma SDK Service" (rzsdkservice.exe) is running
    4. Enable Razer (RGB.NET) device in Aurora's Device Manager
    5. Add a "Razer Chroma" layer to your Aurora profile for the game
    6. Flash the Ducky firmware with RAW_ENABLE=yes
    7. Run this script, then launch your game

Data flow:
    Game -> RzChromaSDK64.dll -> Razer SDK Service -> Aurora (reads shared mem)
         -> Aurora DeviceLedMap (MMF) -> THIS SCRIPT -> Ducky raw HID
"""

import sys
import os
import mmap
import struct
import time
import ctypes
import ctypes.wintypes
import threading
import logging
import argparse

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

# --- Chroma HID Protocol (keyboard firmware side) ---
CMD_SET_LEDS  = 0x01
CMD_ENABLE    = 0x02
CMD_DISABLE   = 0x03
CMD_HEARTBEAT = 0x04
MAX_LEDS_PER_PACKET = 9

# --- LED count ---
DUCKY_LED_COUNT = 69


# =============================================================================
# Aurora DeviceLedMap shared memory reader
# =============================================================================
#
# Aurora writes per-key RGB colors to a Windows named memory-mapped file called
# "DeviceLedMap". The layout is:
#
#   Offset 0..7  : int64  totalSize
#   Offset 8..11 : int32  count (= MaxKeyId, typically 366)
#   Offset 12+   : SimpleColor[] array, indexed by DeviceKeys enum value
#                   Each SimpleColor is 4 bytes: R, G, B, A
#
# An auto-reset event named "DeviceLedMap-updated" is signalled on each frame.
#
# Reference:
#   Aurora/Project-Aurora/AuroraCommon/Data/MemorySharedArrayWrite.cs
#   Aurora/Project-Aurora/AuroraCommon/SimpleColor.cs
#   Aurora/Project-Aurora/AuroraCommon/Devices/DeviceKeys.cs

# --- Aurora DeviceKeys enum (keyboard keys only) ---
# Values from Aurora/Project-Aurora/AuroraCommon/Devices/DeviceKeys.cs
class AuroraKey:
    ESC = 1
    F1 = 2;  F2 = 3;  F3 = 4;  F4 = 5
    F5 = 6;  F6 = 7;  F7 = 8;  F8 = 9
    F9 = 10; F10 = 11; F11 = 12; F12 = 13
    TILDE = 17
    ONE = 18; TWO = 19; THREE = 20; FOUR = 21; FIVE = 22
    SIX = 23; SEVEN = 24; EIGHT = 25; NINE = 26; ZERO = 27
    MINUS = 28; EQUALS = 29; BACKSPACE = 30
    INSERT = 31; HOME = 32; PAGE_UP = 33
    TAB = 38
    Q = 39; W = 40; E = 41; R = 42; T = 43
    Y = 44; U = 45; I = 46; O = 47; P = 48
    OPEN_BRACKET = 49; CLOSE_BRACKET = 50; BACKSLASH = 51
    DELETE = 52; END = 53; PAGE_DOWN = 54
    CAPS_LOCK = 59
    A = 60; S = 61; D = 62; F = 63; G = 64
    H = 65; J = 66; K = 67; L = 68
    SEMICOLON = 69; APOSTROPHE = 70; ENTER = 72
    LEFT_SHIFT = 76
    Z = 78; X = 79; C = 80; V = 81; B = 82
    N = 83; M = 84; COMMA = 85; PERIOD = 86; FORWARD_SLASH = 87
    RIGHT_SHIFT = 88; ARROW_UP = 89
    LEFT_CONTROL = 94; LEFT_WINDOWS = 95; LEFT_ALT = 96
    SPACE = 97
    RIGHT_ALT = 98; RIGHT_WINDOWS = 99
    APPLICATION_SELECT = 100; RIGHT_CONTROL = 101
    ARROW_LEFT = 102; ARROW_DOWN = 103; ARROW_RIGHT = 104
    FN_KEY = 107


# --- Mapping: Aurora DeviceKeys enum value -> Ducky LED index ---
#
# Ducky LED index layout (from keyboard.json rgb_matrix.layout[]):
#  Row 0: Esc(0)  1(1)  2(2)  3(3)  4(4)  5(5)  6(6)  7(7)  8(8)  9(9) 0(10) -(11) =(12) Bksp(13) Del(14)
#  Row 1: Tab(15) Q(16) W(17) E(18) R(19) T(20) Y(21) U(22) I(23) O(24) P(25)  [(26) ](27) \(28) PgUp(29)
#  Row 2: Caps(30) A(31) S(32) D(33) F(34) G(35) H(36) J(37) K(38) L(39) ;(40) '(41) Enter(42)   PgDn(43)
#  Row 3: Shift(44) Z(45) X(46) C(47) V(48) B(49) N(50) M(51) ,(52) .(53) /(54) RShift(55) Up(56)
#  Row 4: Ctrl(57) Alt(58) GUI(59) Space(60)  Alt(61) Fn(62) Ctrl(63) Left(64) Down(65) Right(66) [67,68]

AURORA_TO_DUCKY = {
    # Row 0: Number row
    AuroraKey.ESC:       0,
    AuroraKey.ONE:       1,
    AuroraKey.TWO:       2,
    AuroraKey.THREE:     3,
    AuroraKey.FOUR:      4,
    AuroraKey.FIVE:      5,
    AuroraKey.SIX:       6,
    AuroraKey.SEVEN:     7,
    AuroraKey.EIGHT:     8,
    AuroraKey.NINE:      9,
    AuroraKey.ZERO:      10,
    AuroraKey.MINUS:     11,
    AuroraKey.EQUALS:    12,
    AuroraKey.BACKSPACE: 13,
    AuroraKey.DELETE:    14,

    # Row 1: QWERTY row
    AuroraKey.TAB:           15,
    AuroraKey.Q:             16,
    AuroraKey.W:             17,
    AuroraKey.E:             18,
    AuroraKey.R:             19,
    AuroraKey.T:             20,
    AuroraKey.Y:             21,
    AuroraKey.U:             22,
    AuroraKey.I:             23,
    AuroraKey.O:             24,
    AuroraKey.P:             25,
    AuroraKey.OPEN_BRACKET:  26,
    AuroraKey.CLOSE_BRACKET: 27,
    AuroraKey.BACKSLASH:     28,
    AuroraKey.PAGE_UP:       29,

    # Row 2: Home row
    AuroraKey.CAPS_LOCK:  30,
    AuroraKey.A:          31,
    AuroraKey.S:          32,
    AuroraKey.D:          33,
    AuroraKey.F:          34,
    AuroraKey.G:          35,
    AuroraKey.H:          36,
    AuroraKey.J:          37,
    AuroraKey.K:          38,
    AuroraKey.L:          39,
    AuroraKey.SEMICOLON:  40,
    AuroraKey.APOSTROPHE: 41,
    AuroraKey.ENTER:      42,
    AuroraKey.PAGE_DOWN:  43,

    # Row 3: Shift row
    AuroraKey.LEFT_SHIFT:    44,
    AuroraKey.Z:             45,
    AuroraKey.X:             46,
    AuroraKey.C:             47,
    AuroraKey.V:             48,
    AuroraKey.B:             49,
    AuroraKey.N:             50,
    AuroraKey.M:             51,
    AuroraKey.COMMA:         52,
    AuroraKey.PERIOD:        53,
    AuroraKey.FORWARD_SLASH: 54,
    AuroraKey.RIGHT_SHIFT:   55,
    AuroraKey.ARROW_UP:      56,

    # Row 4: Bottom row
    AuroraKey.LEFT_CONTROL:  57,
    AuroraKey.LEFT_ALT:      58,
    AuroraKey.LEFT_WINDOWS:  59,
    AuroraKey.SPACE:         60,
    AuroraKey.RIGHT_ALT:     61,
    AuroraKey.FN_KEY:        62,
    AuroraKey.RIGHT_CONTROL: 63,
    AuroraKey.ARROW_LEFT:    64,
    AuroraKey.ARROW_DOWN:    65,
    AuroraKey.ARROW_RIGHT:   66,
}

# MMF layout constants
MMF_NAME = "DeviceLedMap"
MMF_EVENT_NAME = "DeviceLedMap-updated"
MMF_HEADER_SIZE = 12          # sizeof(long) + sizeof(int)
MMF_COLOR_SIZE = 4            # R, G, B, A bytes
MMF_MAX_KEY_ID = 366          # Upper bound for DeviceKeys enum
MMF_TOTAL_SIZE = MMF_HEADER_SIZE + MMF_MAX_KEY_ID * MMF_COLOR_SIZE

# Windows API constants
SYNCHRONIZE = 0x00100000
WAIT_OBJECT_0 = 0x00000000
WAIT_TIMEOUT = 0x00000102


class AuroraMMFReader:
    """Reads per-key RGB colors from Aurora's DeviceLedMap shared memory."""

    def __init__(self):
        self._mmf = None
        self._event_handle = None

    def connect(self):
        """Open the DeviceLedMap MMF and update event.

        Returns True if the MMF was opened successfully (Aurora is running).
        """
        try:
            # Open the named memory-mapped file created by Aurora.
            # tagname maps to Windows CreateFileMapping / OpenFileMapping.
            self._mmf = mmap.mmap(-1, MMF_TOTAL_SIZE, tagname=MMF_NAME,
                                  access=mmap.ACCESS_READ)
        except (OSError, mmap.error) as exc:
            log.error("Cannot open Aurora DeviceLedMap: %s", exc)
            log.error("Is Aurora running with a Razer Chroma layer active?")
            return False

        # Open the auto-reset event Aurora signals on each frame.
        kernel32 = ctypes.windll.kernel32
        self._event_handle = kernel32.OpenEventW(
            SYNCHRONIZE, False, MMF_EVENT_NAME
        )
        if not self._event_handle:
            log.warning("Could not open '%s' event — falling back to polling",
                        MMF_EVENT_NAME)

        # Read header to confirm MMF looks valid.
        self._mmf.seek(0)
        header = self._mmf.read(MMF_HEADER_SIZE)
        total_size, count = struct.unpack("<qI", header)
        log.info("DeviceLedMap opened: totalSize=%d count=%d", total_size, count)

        return True

    def disconnect(self):
        if self._mmf:
            self._mmf.close()
            self._mmf = None
        if self._event_handle:
            ctypes.windll.kernel32.CloseHandle(self._event_handle)
            self._event_handle = None

    def wait_for_update(self, timeout_ms=50):
        """Block until Aurora signals a new frame, or timeout.

        Returns True if a new frame is available.
        """
        if self._event_handle:
            ret = ctypes.windll.kernel32.WaitForSingleObject(
                self._event_handle, timeout_ms
            )
            return ret == WAIT_OBJECT_0
        # No event handle — just sleep and return True so caller always reads.
        time.sleep(timeout_ms / 1000.0)
        return True

    def read_frame(self):
        """Read a full frame of Ducky LED colors from the MMF.

        Returns:
            list of DUCKY_LED_COUNT (R, G, B) tuples, or None on error.
        """
        if not self._mmf:
            return None

        led_array = [(0, 0, 0)] * DUCKY_LED_COUNT

        for aurora_key, ducky_idx in AURORA_TO_DUCKY.items():
            offset = MMF_HEADER_SIZE + aurora_key * MMF_COLOR_SIZE
            try:
                self._mmf.seek(offset)
                r, g, b, a = struct.unpack("BBBB", self._mmf.read(4))
            except (struct.error, ValueError):
                continue

            # Premultiply alpha (Aurora uses A=255 for opaque).
            if a == 0:
                continue
            if a < 255:
                r = (r * a) // 255
                g = (g * a) // 255
                b = (b * a) // 255

            led_array[ducky_idx] = (r, g, b)

        return led_array


# =============================================================================
# Ducky One 2 SF raw HID communication (unchanged from previous version)
# =============================================================================

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
            if i.get("usage_page") == RAW_USAGE_PAGE
            and i.get("usage") == RAW_USAGE_ID
        ]

        if not raw_interfaces:
            log.warning(
                "Ducky One 2 SF raw HID interface not found "
                "(VID=0x%04X PID=0x%04X)", DUCKY_VID, DUCKY_PID,
            )
            log.info("Available devices:")
            for d in device_interfaces:
                log.info(
                    "  path=%s usage_page=0x%04X usage=0x%02X",
                    d.get("path", b"").decode(errors="replace"),
                    d.get("usage_page", 0),
                    d.get("usage", 0),
                )
            return False

        self.device = hid.device()
        self.device.open_path(raw_interfaces[0]["path"])
        log.info(
            "Connected: %s %s",
            self.device.get_manufacturer_string(),
            self.device.get_product_string(),
        )
        return True

    def disconnect(self):
        if self.device:
            self.device.close()
            self.device = None

    def _send(self, data):
        """Send a 32-byte raw HID report. Returns response or None."""
        if not self.device:
            return None
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


# =============================================================================
# Main bridge loop
# =============================================================================

def frames_are_equal(a, b):
    """Fast comparison of two LED frame arrays."""
    if a is None or b is None:
        return False
    return a == b


def main():
    parser = argparse.ArgumentParser(
        description="Aurora RGB -> Ducky One 2 SF Chroma Bridge",
    )
    parser.add_argument(
        "--poll-ms", type=int, default=50,
        help="Event wait timeout / poll interval in ms (default: 50 = ~20 fps)",
    )
    parser.add_argument(
        "--heartbeat-interval", type=float, default=2.0,
        help="Seconds between keyboard heartbeat packets (default: 2.0)",
    )
    parser.add_argument(
        "--retry-interval", type=float, default=5.0,
        help="Seconds between Aurora MMF connection retries (default: 5.0)",
    )
    args = parser.parse_args()

    log.info("Ducky One 2 SF Aurora Chroma Bridge")
    log.info("VID=0x%04X PID=0x%04X", DUCKY_VID, DUCKY_PID)

    if os.name != "nt":
        log.error("This script requires Windows (named MMF + event handles).")
        sys.exit(1)

    # --- Connect to keyboard ---
    ducky = DuckyHID()
    if not ducky.connect():
        log.error("Failed to connect to keyboard. Is it plugged in?")
        log.error("Make sure the firmware has RAW_ENABLE=yes and is flashed.")
        sys.exit(1)

    ducky.enable_chroma()
    log.info("Keyboard Chroma mode enabled")

    # --- Connect to Aurora MMF (retry loop) ---
    aurora = AuroraMMFReader()
    while not aurora.connect():
        log.info("Waiting for Aurora... (retrying in %.0fs)", args.retry_interval)
        time.sleep(args.retry_interval)

    log.info("Bridge active — reading Aurora DeviceLedMap, forwarding to Ducky")
    log.info("Press Ctrl+C to stop")

    prev_frame = None
    last_heartbeat = time.monotonic()
    frames_sent = 0
    frames_skipped = 0
    t_start = time.monotonic()

    try:
        while True:
            # Wait for Aurora to signal a new frame (or poll timeout).
            aurora.wait_for_update(timeout_ms=args.poll_ms)

            # Send periodic heartbeat to keep keyboard in Chroma mode.
            now = time.monotonic()
            if now - last_heartbeat >= args.heartbeat_interval:
                ducky.heartbeat()
                last_heartbeat = now

            # Read the current frame from shared memory.
            frame = aurora.read_frame()
            if frame is None:
                continue

            # Only send to keyboard if colors actually changed.
            if frames_are_equal(frame, prev_frame):
                frames_skipped += 1
                continue

            ducky.set_all_leds(frame)
            prev_frame = frame
            frames_sent += 1

            # Periodic stats log.
            elapsed = now - t_start
            if elapsed >= 30.0:
                fps = frames_sent / elapsed if elapsed > 0 else 0
                log.info(
                    "Stats: %.1f fps, %d frames sent, %d skipped (no change)",
                    fps, frames_sent, frames_skipped,
                )
                frames_sent = 0
                frames_skipped = 0
                t_start = now

    except KeyboardInterrupt:
        log.info("Interrupted by user")
    finally:
        log.info("Shutting down...")
        aurora.disconnect()
        ducky.disable_chroma()
        ducky.disconnect()
        log.info("Done")


if __name__ == "__main__":
    main()
