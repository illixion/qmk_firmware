# illixion's QMK Fork — OpenRGB Direct Protocol (QMKD)

This fork of [QMK Firmware](https://github.com/qmk/qmk_firmware) adds the **QMK Direct Protocol (QMKD)**, a lightweight protocol that gives any QMK keyboard universal [OpenRGB](https://openrgb.org/) compatibility for host-controlled per-key lighting.

## What this fork adds

**`qmk_openrgb_direct.h`** — a single drop-in header that any QMK keymap can include. It extends the standard raw HID interface with three self-description commands so that host software (OpenRGB, Aurora, custom scripts) can discover the keyboard's LED layout, matrix dimensions, and keycodes at runtime. No hardcoded layouts needed on the host side.

The protocol uses compact 32-byte packets on the existing QMK raw HID endpoint (usage page 0xFF60, usage 0x61) and adds zero overhead when host control is not active.

### Supported data flows

Using OpenRGB as the host (recommended):

```
Game -> Razer Chroma SDK -> Aurora -> OpenRGB -> QMKD -> Keyboard
```

Standalone Python bridge (no OpenRGB needed):

```
Game -> Razer Chroma SDK -> Aurora -> DeviceLedMap (shared memory) -> chroma_bridge.py -> Keyboard
```

### Adding QMKD to your keyboard

1. Copy `qmk_openrgb_direct.h` into your keymap directory
2. Include it in `keymap.c` and wire up the two callbacks:

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
    if (qmkd_rgb_matrix_indicator(led_min, led_max)) {
        return true;
    }
    return false;
}
```

3. Ensure `RAW_ENABLE = yes` in `rules.mk`
4. Add your keyboard's VID/PID to `QMKDirectControllerDetect.cpp` in the [OpenRGB fork](#openrgb-controller)
5. Flash — OpenRGB auto-detects the keyboard

### Protocol summary

| Command | ID | Direction | Description |
|---------|-----|-----------|-------------|
| SET_LEDS | 0x01 | Host -> KB | Batch-set up to 9 LEDs per packet |
| ENABLE | 0x02 | Host -> KB | Enter host-controlled mode |
| DISABLE | 0x03 | Host -> KB | Return to local RGB effects |
| HEARTBEAT | 0x04 | Host -> KB | Keep-alive (configurable timeout) |
| GET_PROTOCOL | 0x05 | Host -> KB | Returns `"QMKD"` magic + version |
| GET_DEVICE | 0x06 | Host -> KB | LED count, matrix dims, capabilities |
| GET_LED_MAP | 0x07 | Host -> KB | Per-LED position, flags, keycode |

### OpenRGB controller

The `openrgb_controller/` directory under the reference keymap contains a generic OpenRGB controller. To use it, copy the 5 files into `Controllers/QMKDirectController/` in your OpenRGB fork — the build system auto-discovers them.

### Reference implementation

See [`keyboards/ducky/one2sf/keymaps/illixion/`](/keyboards/ducky/one2sf/keymaps/illixion/) for a complete working example on a Ducky One 2 SF, including the firmware integration, OpenRGB controller, Aurora Python bridge, and keymap-level README with full protocol documentation.

---

# Quantum Mechanical Keyboard Firmware

[![Current Version](https://img.shields.io/github/tag/qmk/qmk_firmware.svg)](https://github.com/qmk/qmk_firmware/tags)
[![Discord](https://img.shields.io/discord/440868230475677696.svg)](https://discord.gg/qmk)
[![Docs Status](https://img.shields.io/badge/docs-ready-orange.svg)](https://docs.qmk.fm)
[![GitHub contributors](https://img.shields.io/github/contributors/qmk/qmk_firmware.svg)](https://github.com/qmk/qmk_firmware/pulse/monthly)
[![GitHub forks](https://img.shields.io/github/forks/qmk/qmk_firmware.svg?style=social&label=Fork)](https://github.com/qmk/qmk_firmware/)

This is a keyboard firmware based on the [tmk\_keyboard firmware](https://github.com/tmk/tmk_keyboard) with some useful features for Atmel AVR and ARM controllers, and more specifically, the [OLKB product line](https://olkb.com), the [ErgoDox EZ](https://ergodox-ez.com) keyboard, and the Clueboard product line.

## Documentation

* [See the official documentation on docs.qmk.fm](https://docs.qmk.fm)

The docs are powered by [VitePress](https://vitepress.dev/). They are also viewable offline; see [Previewing the Documentation](https://docs.qmk.fm/#/contributing?id=previewing-the-documentation) for more details.

You can request changes by making a fork and opening a [pull request](https://github.com/qmk/qmk_firmware/pulls).

## Supported Keyboards

* [Planck](/keyboards/planck/)
* [Preonic](/keyboards/preonic/)
* [ErgoDox EZ](/keyboards/ergodox_ez/)
* [Clueboard](/keyboards/clueboard/)
* [Cluepad](/keyboards/clueboard/17/)
* [Atreus](/keyboards/atreus/)

The project also includes community support for [lots of other keyboards](/keyboards/).

## Maintainers

QMK is developed and maintained by Jack Humbert of OLKB with contributions from the community, and of course, [Hasu](https://github.com/tmk). The OLKB product firmwares are maintained by [Jack Humbert](https://github.com/jackhumbert), the Ergodox EZ by [ZSA Technology Labs](https://github.com/zsa), the Clueboard by [Zach White](https://github.com/skullydazed), and the Atreus by [Phil Hagelberg](https://github.com/technomancy).

## Official Website

[qmk.fm](https://qmk.fm) is the official website of QMK, where you can find links to this page, the documentation, and the keyboards supported by QMK.
