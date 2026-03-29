# illixion's Ducky One 2 SF keymap

Custom ANSI keymap with:
- CMD+Esc sends ` (grave) for macOS app switching, Esc otherwise
- Mouse keys with tuned acceleration
- All RGB matrix effects enabled
- Full RGB control on layer 2 (Right Fn + Z to toggle, X to cycle modes, ArrowUp/Down to adjust brightness, ArrowLeft/Right to adjust speed)
- **Auto OS detection**: Automatically swaps Alt/GUI when switching between macOS and Windows/Linux
- **Razer Chroma bridge**: Receives per-key RGB from games (like Cyberpunk 2077) via a host-side Python bridge

## Auto OS Detection

The firmware detects the host OS via USB enumeration and automatically adjusts the modifier layout:

- **macOS/iOS**: Native layout — Ctrl, Option(Alt), Cmd(GUI)
- **Windows/Linux**: Swapped — Ctrl, Win(GUI), Alt

No manual AG_SWAP needed (though it's still available on Layer 2 as a manual override).

## Razer Chroma Bridge

Games that support Razer Chroma (Cyberpunk 2077, Overwatch, etc.) can control the keyboard's per-key RGB lighting through a Python bridge script that emulates the Chroma REST API.

### Setup

```bash
pip install flask hid
python chroma_bridge.py
```

The bridge:
1. Connects to the keyboard via raw HID
2. Starts a local REST API on port 54235 (Razer Chroma SDK default)
3. Games auto-detect it as a Chroma-compatible device
4. Per-key colors from the game are translated to the Ducky's 69-LED layout

### Protocol

The firmware accepts 32-byte raw HID packets:

| Command | Byte 0 | Payload | Description |
|---------|--------|---------|-------------|
| SET_LEDS | 0x01 | start, count, R,G,B... | Set up to 9 LEDs per packet |
| ENABLE | 0x02 | — | Enable Chroma passthrough mode |
| DISABLE | 0x03 | — | Return to normal RGB effects |
| HEARTBEAT | 0x04 | — | Keep-alive (5s timeout) |

When Chroma mode is active, normal RGB effects are overridden. If no heartbeat is received for 5 seconds, it automatically falls back to normal effects.

## Build

    cargo install nu-isp-cli
    qmk compile -kb ducky/one2sf/1967st -km illixion

## Flash

Hold D+L while plugging in the keyboard to enter bootloader mode, then:

    nu-isp-cli flash ducky_one2sf_1967st_illixion.bin
