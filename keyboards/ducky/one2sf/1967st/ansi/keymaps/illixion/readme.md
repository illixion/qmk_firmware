# illixion's Ducky One 2 SF keymap

Custom ANSI keymap with:
- Esc key (grave-escape): `Cmd+Esc` and `Shift+Esc` send `` ` `` (Shift turns it into `~`), Esc otherwise
- Mouse keys with tuned acceleration
- All RGB matrix effects enabled
- Full RGB control on layer 2 (Right Fn + Z to toggle, X to cycle modes, C and V to decrease/increase speed, ArrowUp/Down to adjust brightness, ArrowLeft/Right to adjust color)
- **Mac/Windows layout**: auto OS detection swaps Alt/GUI, with a host-authoritative override for macOS (see below) and a manual toggle on layer 2
- **Privacy blackout**: RGB turns off while macOS secure input is active (password fields), so reactive effects can't reveal keypresses — driven by the Mac companion in [`host/`](host/)
- **OpenRGB / Aurora support**: Universal host-controlled lighting via the QMK Direct Protocol (QMKD), compatible with OpenRGB, Aurora RGB, and any QMKD-aware host

## Mac/Windows layout

The firmware detects the host OS via USB enumeration and automatically adjusts the modifier layout:

- **macOS/iOS**: Native layout — Ctrl, Option(Alt), Cmd(GUI)
- **Windows/Linux**: Swapped — Ctrl, Win(GUI), Alt

A manual toggle (`MACWIN_TOGG`) is on Layer 2 and always wins once pressed.

> **macOS caveat:** QMK's USB OS detection mis-fingerprints this Mac as Linux —
> macOS serves cached string descriptors, so it skips the probes the macOS
> heuristic needs and falls through to the Linux branch (swapping into the
> Windows layout). The fix is host-authoritative: the Mac companion in
> [`host/`](host/) asserts the layout over raw HID (`HOSTCMD_SET_LAYOUT`, 0xA1),
> overriding detection. See `host/README.md` for the why and how.

## Mac companion (`host/`)

[`host/`](host/) holds a small native Swift daemon (`ducky-maclayout.swift`) and
a launchd agent that run on the Mac. The Mac knows things the keyboard can't
reliably detect over USB, so it tells the keyboard via raw HID:

- **Layout** — asserts the Mac layout on every attach (`0xA1`), fixing the
  macOS OS-detection misfire described above.
- **Privacy blackout** — watches macOS secure input and sends `HOSTCMD_SET_PRIVACY`
  (`0xA2`) so the keyboard blacks out all LEDs while a password field is focused,
  overriding both local effects and host/OpenRGB frames.

It uses `IOHIDManager` on the vendor raw usage page (no Input Monitoring grant
needed) and needs no Python dependency. Build/install/uninstall steps are in
[`host/README.md`](host/README.md).

## QMK Direct Protocol (QMKD)

The firmware implements the QMK Direct Protocol v1.0 via `qmk_openrgb_direct.h`, a drop-in header that gives any QMK keyboard universal OpenRGB compatibility. Host software can discover the keyboard's layout dynamically — no hardcoded key maps needed on the host side.

### How it works

The recommended data flow for game RGB effects uses OpenRGB as the intermediary:

```
Game -> RzChromaSDK64.dll -> Aurora -> OpenRGB -> QMK Direct Protocol -> Keyboard
```

A standalone Python bridge (`chroma_bridge.py`) is also included for use without OpenRGB:

```
Game -> RzChromaSDK64.dll -> Aurora -> DeviceLedMap (shared memory) -> chroma_bridge.py -> Keyboard
```

### Protocol

32-byte raw HID packets on usage page 0xFF60, usage 0x61.

**Control commands** (LED operations):

| Command | Byte 0 | Payload | Description |
|---------|--------|---------|-------------|
| SET_LEDS | 0x01 | start, count, R,G,B... | Set up to 9 LEDs per packet |
| ENABLE | 0x02 | — | Enter host-controlled mode |
| DISABLE | 0x03 | — | Return to local RGB effects |
| HEARTBEAT | 0x04 | — | Keep-alive (resets 5s timeout) |

**Discovery commands** (self-description, queried once at connection):

| Command | Byte 0 | Response | Description |
|---------|--------|----------|-------------|
| GET_PROTOCOL | 0x05 | `"QMKD"`, major, minor | Protocol magic + version |
| GET_DEVICE | 0x06 | led_count, rows, cols, max_per_pkt, timeout | Device capabilities |
| GET_LED_MAP | 0x07 | x, y, flags, row, col, keycode per LED | Physical layout (4 LEDs/packet) |

The GET_PROTOCOL probe lets hosts positively identify QMKD-compatible devices. GET_LED_MAP returns the physical position, matrix coordinates, and layer-0 keycode for each LED, allowing hosts like OpenRGB to reconstruct the full keyboard layout automatically.

### Adding QMKD to another QMK keyboard

1. Copy `qmk_openrgb_direct.h` into your keymap directory
2. In your `keymap.c`, include the header and wire it up:

```c
#include "qmk_openrgb_direct.h"

void raw_hid_receive(uint8_t *data, uint8_t length) {
    uint8_t response[RAW_EPSIZE];
    memset(response, 0, RAW_EPSIZE);
    if (!qmkd_process_packet(data, length, response)) {
        response[0] = QMKD_RSP_UNKNOWN;
    }
    raw_hid_send(response, RAW_EPSIZE);
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    qmkd_rgb_matrix_indicator(led_min, led_max);
    return false;
}
```

3. Ensure `RAW_ENABLE = yes` in `rules.mk`
4. Add your keyboard's VID/PID to `QMKDirectControllerDetect.cpp` in the OpenRGB fork
5. Flash and rebuild — OpenRGB will auto-detect your keyboard

### OpenRGB controller

The `openrgb_controller/` directory contains a generic OpenRGB controller that works with any QMKD keyboard. To use it in an OpenRGB fork, copy the 5 files into `Controllers/QMKDirectController/` — OpenRGB's build system auto-discovers them via glob patterns.

### Aurora Python bridge

For standalone use without OpenRGB (Windows only):

```bash
pip install hidapi
python chroma_bridge.py
```

The bridge auto-detects QMKD v1 firmware and queries LED count and timing from the device. For non-Ducky keyboards, pass `--vid` and `--pid`.

| Flag | Default | Description |
|------|---------|-------------|
| `--vid` | 0x445B | USB Vendor ID |
| `--pid` | 0x07AF | USB Product ID |
| `--poll-ms` | 50 | Event wait timeout in ms (~20 fps) |
| `--heartbeat-interval` | auto | Seconds between heartbeats (auto = half firmware timeout) |
| `--retry-interval` | 5.0 | Seconds between Aurora connection retries |

## Build

    cargo install nu-isp-cli
    qmk compile -kb ducky/one2sf/1967st/ansi -km illixion

## Flash

Hold D+L while plugging in the keyboard to enter bootloader mode, then:

    nu-isp-cli flash ducky_one2sf_1967st_ansi_illixion.bin
