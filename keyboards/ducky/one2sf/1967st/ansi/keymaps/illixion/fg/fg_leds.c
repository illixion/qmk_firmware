// fg_leds.c — see fg_leds.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fg_leds.h"

#include "quantum.h"
#include "rgb_matrix.h"

void last_matrix_activity_trigger(void);

typedef struct {
    uint8_t  r, g, b;
    bool     active;
    bool     has_ttl;
    uint32_t expires_at;   // timer_read32() value, valid when has_ttl
} fg_led_t;

static fg_led_t overrides[RGB_MATRIX_LED_COUNT];

uint8_t fg_leds_total(void) { return RGB_MATRIX_LED_COUNT; }

bool fg_leds_set(uint8_t led, uint8_t r, uint8_t g, uint8_t b, uint16_t ttl_s) {
    if (led >= RGB_MATRIX_LED_COUNT) return false;
    fg_led_t *o = &overrides[led];
    o->r = r; o->g = g; o->b = b;
    o->active  = true;
    o->has_ttl = ttl_s != 0;
    o->expires_at = timer_read32() + (uint32_t)ttl_s * 1000UL;
    last_matrix_activity_trigger();   // wake a sleeping matrix so the change shows now
    return true;
}

void fg_leds_reset(uint8_t led) {
    if (led >= RGB_MATRIX_LED_COUNT) return;
    overrides[led].active = false;
    last_matrix_activity_trigger();
}

void fg_leds_reset_all(void) {
    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) overrides[i].active = false;
    last_matrix_activity_trigger();
}

static bool expired(const fg_led_t *o) {
    return o->has_ttl && timer_expired32(timer_read32(), o->expires_at);
}

bool fg_leds_get(uint8_t led, uint8_t rgb[3], uint16_t *ttl_left_s) {
    if (led >= RGB_MATRIX_LED_COUNT) return false;
    fg_led_t *o = &overrides[led];
    if (!o->active) return false;
    if (expired(o)) { o->active = false; return false; }
    rgb[0] = o->r; rgb[1] = o->g; rgb[2] = o->b;
    uint32_t left_ms = o->has_ttl ? (o->expires_at - timer_read32()) : 0;
    *ttl_left_s = (uint16_t)((left_ms + 999) / 1000);
    return true;
}

uint8_t fg_leds_count(void) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
        fg_led_t *o = &overrides[i];
        if (o->active && expired(o)) o->active = false;
        if (o->active) n++;
    }
    return n;
}

void fg_leds_render(uint8_t led_min, uint8_t led_max) {
    for (uint8_t i = led_min; i < led_max && i < RGB_MATRIX_LED_COUNT; i++) {
        fg_led_t *o = &overrides[i];
        if (!o->active) continue;
        if (expired(o)) { o->active = false; continue; }
        rgb_matrix_set_color(i, o->r, o->g, o->b);
    }
}
