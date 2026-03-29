# illixion's Ducky One 2 SF keymap

Custom ANSI keymap with:
- CMD+Esc sends ` (grave) for macOS app switching, Esc otherwise
- Mouse keys with tuned acceleration
- All RGB matrix effects enabled
- Full RGB control on layer 2 (Right Fn + Z to toggle, X to cycle modes, C and V to decrease/increase speed, ArrowUp/Down to adjust brightness, ArrowLeft/Right to adjust color)
- **Auto OS detection**: Automatically swaps Alt/GUI when switching between macOS and Windows/Linux
- **Razer Chroma bridge**: Receives per-key RGB from games (like Cyberpunk 2077) via Aurora RGB and a host-side Python bridge

## Auto OS Detection

The firmware detects the host OS via USB enumeration and automatically adjusts the modifier layout:

- **macOS/iOS**: Native layout — Ctrl, Option(Alt), Cmd(GUI)
- **Windows/Linux**: Swapped — Ctrl, Win(GUI), Alt

No manual AG_SWAP needed (though it's still available on Layer 2 as a manual override).

## Razer Chroma Bridge

Games that support Razer Chroma (Cyberpunk 2077, Overwatch, etc.) can control the keyboard's per-key RGB lighting through [Aurora RGB](https://www.project-aurora.com/) and a Python bridge script.

Aurora intercepts the native Razer Chroma SDK DLL calls that games make and exposes per-key color data in a shared memory region. The bridge reads that shared memory and forwards colors to the Ducky via raw HID.

```
Game -> RzChromaSDK64.dll -> Razer SDK Service -> Aurora -> DeviceLedMap (shared memory) -> chroma_bridge.py -> Ducky raw HID
```

### Prerequisites

- Windows (required for shared memory and event handles)
- [Aurora RGB](https://www.project-aurora.com/) installed and running
- Razer Chroma SDK installed via Aurora settings
- "Razer Chroma SDK Service" (`rzsdkservice.exe`) running
- Razer (RGB.NET) device enabled in Aurora's Device Manager
- A "Razer Chroma" layer added to your Aurora profile for the game

### Setup

```bash
pip install hidapi
python chroma_bridge.py
```

Optional flags:

| Flag | Default | Description |
|------|---------|-------------|
| `--poll-ms` | 50 | Event wait timeout in ms (~20 fps) |
| `--heartbeat-interval` | 2.0 | Seconds between keyboard heartbeats |
| `--retry-interval` | 5.0 | Seconds between Aurora connection retries |

The bridge:
1. Connects to the keyboard via raw HID
2. Opens Aurora's `DeviceLedMap` shared memory (retries until Aurora is running)
3. Waits on the `DeviceLedMap-updated` event for each new frame
4. Maps Aurora's DeviceKeys to the Ducky's 69-LED layout
5. Sends changed frames to the keyboard (skips identical frames)

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
