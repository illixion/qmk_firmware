// fg_leds.h — per-key LED override layer.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A host can pin any LED to a colour (optionally for a limited time). The
// overrides are drawn after the animation and after any OpenRGB/QMKD host frame,
// every frame, so they persist through every effect. RAM only: nothing survives a
// reboot, and a stuck status colour can never outlive the keyboard being replugged.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// ttl_s == 0 keeps the override until it is reset. Returns false for a bad LED index.
bool    fg_leds_set(uint8_t led, uint8_t r, uint8_t g, uint8_t b, uint16_t ttl_s);
void    fg_leds_reset(uint8_t led);
void    fg_leds_reset_all(void);
// Fills rgb[3] and the remaining seconds (0 = no expiry). Returns false if LED has no override.
bool    fg_leds_get(uint8_t led, uint8_t rgb[3], uint16_t *ttl_left_s);
uint8_t fg_leds_count(void);
uint8_t fg_leds_total(void);
// Draw the active overrides for the LED range the matrix is currently rendering.
void    fg_leds_render(uint8_t led_min, uint8_t led_max);
