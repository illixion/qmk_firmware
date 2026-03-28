# Ducky One 2 SF - QMK Fork with RGB Support

A 65% keyboard by Ducky, running QMK firmware with full RGB LED support.

This is a maintained fork of [f7urry's](https://github.com/f7urry) original Ducky One 2 SF QMK port, rebased onto the latest QMK master. The key addition is a custom MBIA045 LED driver by [hugglesfox/qmk_firmware](https://github.com/hugglesfox/qmk_firmware/tree/one2sf_rgb_fishman) that enables per-key RGB lighting with all QMK RGB Matrix effects.

## What this fork changes

Compared to upstream QMK (which includes the One 2 SF but without working LEDs):

- **RGB LED driver** (`1967st.c`) - Custom MBIA045 16-bit PWM driver for the 69 per-key RGB LEDs
- **RGB Matrix configuration** in `keyboard.json` - LED positions and flags for all keys
- **No changes to QMK core** - all modifications are contained within the keyboard directory

## Supported hardware

* [DKON1967ST](1967st/) - NUC123SD4AN0 + MBI5043GP
* Only ANSI layout is tested; ISO compiles but is **untested**

## Getting started

### 1. Set up the QMK build environment

Follow the [QMK setup guide](https://docs.qmk.fm/#/newbs_getting_started) for your OS, but use this repository instead of the official one:

    git clone <this-repo-url> qmk_firmware
    cd qmk_firmware
    qmk setup -H .

### 2. Build the firmware

Using the default keymap:

    qmk compile -kb ducky/one2sf/1967st -km default

Or use a custom keymap (e.g. `illixion`):

    qmk compile -kb ducky/one2sf/1967st -km illixion

### 3. Enter bootloader mode

Hold **D+L** while plugging in the keyboard.

### 4. Flash the firmware

Install the flash tool:

    cargo install nu-isp-cli

Then flash:

    nu-isp-cli flash ducky_one2sf_1967st_default.bin

### Creating your own keymap

    qmk new-keymap -kb ducky/one2sf/1967st

To enable RGB effects in your keymap, add `#define ENABLE_RGB_MATRIX_*` entries to your keymap's `config.h`. See the `illixion` keymap for an example that enables all available effects.
