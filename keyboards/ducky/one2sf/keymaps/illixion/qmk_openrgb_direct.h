/*---------------------------------------------------------*\
| qmk_openrgb_direct.h                                     |
|                                                           |
|   QMK Direct Protocol (QMKD) v1.0                        |
|   Drop-in header for universal OpenRGB keyboard support   |
|                                                           |
|   Include this in your keymap.c and call                  |
|   qmkd_process_packet() from raw_hid_receive().          |
|                                                           |
|   Requirements:                                           |
|     - RAW_ENABLE = yes in rules.mk                        |
|     - RGB_MATRIX_ENABLE = yes (or equivalent)             |
|                                                           |
|   Protocol uses 32-byte raw HID packets on the standard   |
|   QMK raw HID interface (usage page 0xFF60, usage 0x61). |
|                                                           |
|   illixion                                     2026       |
|   SPDX-License-Identifier: GPL-2.0-or-later              |
\*---------------------------------------------------------*/

#pragma once

#include "quantum.h"
#include "raw_hid.h"
#include "keymap_introspection.h"

/*---------------------------------------------------------*\
| Protocol version                                          |
\*---------------------------------------------------------*/
#define QMKD_VERSION_MAJOR  1
#define QMKD_VERSION_MINOR  0

/*---------------------------------------------------------*\
| Magic identifier — host sends GET_PROTOCOL (0x05) and     |
| checks for this in the response to confirm the device     |
| speaks QMK Direct Protocol                                |
\*---------------------------------------------------------*/
#define QMKD_MAGIC_0  'Q'
#define QMKD_MAGIC_1  'M'
#define QMKD_MAGIC_2  'K'
#define QMKD_MAGIC_3  'D'

/*---------------------------------------------------------*\
| Commands                                                  |
|                                                           |
| Control (v0 — LED operations):                            |
|   0x01  SET_LEDS     Batch-set LED colors                 |
|   0x02  ENABLE       Enter host-controlled lighting mode  |
|   0x03  DISABLE      Return to local RGB effects          |
|   0x04  HEARTBEAT    Keep-alive (resets timeout)          |
|                                                           |
| Discovery (v1 — self-description):                        |
|   0x05  GET_PROTOCOL Protocol magic + version             |
|   0x06  GET_DEVICE   LED count, matrix dimensions, caps   |
|   0x07  GET_LED_MAP  Per-LED position, flags, keycode     |
\*---------------------------------------------------------*/
#define QMKD_CMD_SET_LEDS       0x01
#define QMKD_CMD_ENABLE         0x02
#define QMKD_CMD_DISABLE        0x03
#define QMKD_CMD_HEARTBEAT      0x04
#define QMKD_CMD_GET_PROTOCOL   0x05
#define QMKD_CMD_GET_DEVICE     0x06
#define QMKD_CMD_GET_LED_MAP    0x07

#define QMKD_RSP_UNKNOWN        0xFF

/*---------------------------------------------------------*\
| Tunables — override in config.h before including this     |
\*---------------------------------------------------------*/
#ifndef QMKD_TIMEOUT_MS
#   define QMKD_TIMEOUT_MS      5000
#endif

#ifndef QMKD_MAX_LEDS_PER_PKT
#   define QMKD_MAX_LEDS_PER_PKT  9   /* 3 header + 9*3 = 30 bytes */
#endif

/* LED map entries per packet: 7 bytes each, 4 fit in 32-byte packet */
#define QMKD_LEDS_PER_MAP_PKT   4

#ifndef RAW_EPSIZE
#   define RAW_EPSIZE 32
#endif

/*---------------------------------------------------------*\
| State                                                     |
\*---------------------------------------------------------*/
static bool     qmkd_active          = false;
static uint32_t qmkd_last_heartbeat  = 0;
static uint8_t  qmkd_colors[RGB_MATRIX_LED_COUNT][3];

/*---------------------------------------------------------*\
| Reverse lookup: LED index -> (matrix row, matrix col)     |
| Built lazily on first discovery query                     |
\*---------------------------------------------------------*/
static uint8_t  qmkd_led_row[RGB_MATRIX_LED_COUNT];
static uint8_t  qmkd_led_col[RGB_MATRIX_LED_COUNT];
static bool     qmkd_lookup_ready = false;

static void qmkd_build_lookup(void) {
    if (qmkd_lookup_ready) return;

    /* Default: 0xFF means "not in key matrix" (e.g. underglow) */
    memset(qmkd_led_row, 0xFF, sizeof(qmkd_led_row));
    memset(qmkd_led_col, 0xFF, sizeof(qmkd_led_col));

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            uint8_t led = g_led_config.matrix_co[r][c];
            if (led != NO_LED && led < RGB_MATRIX_LED_COUNT) {
                qmkd_led_row[led] = r;
                qmkd_led_col[led] = c;
            }
        }
    }
    qmkd_lookup_ready = true;
}

/*---------------------------------------------------------*\
| qmkd_process_packet                                      |
|                                                           |
|   Process one raw HID packet. Returns true if the command |
|   was recognised (response is filled in), false otherwise |
|   so callers can handle their own custom commands.        |
|                                                           |
|   Usage in keymap.c:                                      |
|                                                           |
|     void raw_hid_receive(uint8_t *data, uint8_t length) { |
|         uint8_t resp[RAW_EPSIZE];                         |
|         memset(resp, 0, RAW_EPSIZE);                      |
|         if (!qmkd_process_packet(data, length, resp)) {   |
|             resp[0] = 0xFF;  // unknown command            |
|         }                                                  |
|         raw_hid_send(resp, RAW_EPSIZE);                   |
|     }                                                      |
\*---------------------------------------------------------*/
static bool qmkd_process_packet(uint8_t *data, uint8_t length, uint8_t *response) {
    (void)length;

    switch (data[0]) {

        /*-----------------------------------------------------*\
        | 0x01 SET_LEDS                                         |
        |   [cmd, start, count(<=9), R,G,B, R,G,B, ...]        |
        \*-----------------------------------------------------*/
        case QMKD_CMD_SET_LEDS: {
            uint8_t start = data[1];
            uint8_t count = data[2];
            if (count > QMKD_MAX_LEDS_PER_PKT) count = QMKD_MAX_LEDS_PER_PKT;
            if (start + count > RGB_MATRIX_LED_COUNT) {
                count = (start < RGB_MATRIX_LED_COUNT)
                      ? RGB_MATRIX_LED_COUNT - start : 0;
            }
            for (uint8_t i = 0; i < count; i++) {
                uint8_t idx = start + i;
                qmkd_colors[idx][0] = data[3 + i * 3];
                qmkd_colors[idx][1] = data[3 + i * 3 + 1];
                qmkd_colors[idx][2] = data[3 + i * 3 + 2];
            }
            qmkd_last_heartbeat = timer_read32();
            response[0] = QMKD_CMD_SET_LEDS;
            return true;
        }

        /*-----------------------------------------------------*\
        | 0x02 ENABLE                                           |
        \*-----------------------------------------------------*/
        case QMKD_CMD_ENABLE:
            qmkd_active = true;
            qmkd_last_heartbeat = timer_read32();
            memset(qmkd_colors, 0, sizeof(qmkd_colors));
            response[0] = QMKD_CMD_ENABLE;
            return true;

        /*-----------------------------------------------------*\
        | 0x03 DISABLE                                          |
        \*-----------------------------------------------------*/
        case QMKD_CMD_DISABLE:
            qmkd_active = false;
            response[0] = QMKD_CMD_DISABLE;
            return true;

        /*-----------------------------------------------------*\
        | 0x04 HEARTBEAT                                        |
        |   Response byte 1: 1 if active, 0 if not             |
        \*-----------------------------------------------------*/
        case QMKD_CMD_HEARTBEAT:
            qmkd_last_heartbeat = timer_read32();
            response[0] = QMKD_CMD_HEARTBEAT;
            response[1] = qmkd_active ? 1 : 0;
            return true;

        /*-----------------------------------------------------*\
        | 0x05 GET_PROTOCOL                                     |
        |   Response: [0x05, 'Q','M','K','D', major, minor]    |
        \*-----------------------------------------------------*/
        case QMKD_CMD_GET_PROTOCOL:
            response[0] = QMKD_CMD_GET_PROTOCOL;
            response[1] = QMKD_MAGIC_0;
            response[2] = QMKD_MAGIC_1;
            response[3] = QMKD_MAGIC_2;
            response[4] = QMKD_MAGIC_3;
            response[5] = QMKD_VERSION_MAJOR;
            response[6] = QMKD_VERSION_MINOR;
            return true;

        /*-----------------------------------------------------*\
        | 0x06 GET_DEVICE                                       |
        |   Response: [0x06, led_count, matrix_rows,            |
        |              matrix_cols, max_leds_per_pkt,            |
        |              timeout_seconds]                          |
        |                                                       |
        |   The host reads USB product/manufacturer strings     |
        |   for the human-readable device name.                 |
        \*-----------------------------------------------------*/
        case QMKD_CMD_GET_DEVICE:
            response[0] = QMKD_CMD_GET_DEVICE;
            response[1] = RGB_MATRIX_LED_COUNT;
            response[2] = MATRIX_ROWS;
            response[3] = MATRIX_COLS;
            response[4] = QMKD_MAX_LEDS_PER_PKT;
            response[5] = (uint8_t)(QMKD_TIMEOUT_MS / 1000);
            return true;

        /*-----------------------------------------------------*\
        | 0x07 GET_LED_MAP                                      |
        |   Request:  [0x07, start_index]                       |
        |   Response: [0x07, start_index, count,                |
        |              {x, y, flags, row, col, kc_lo, kc_hi}   |
        |              * count ]                                |
        |                                                       |
        |   7 bytes per LED, up to 4 per 32-byte packet.       |
        |   row/col = 0xFF for LEDs outside the key matrix.    |
        |   kc = layer-0 keycode (QMK basic keycode).          |
        \*-----------------------------------------------------*/
        case QMKD_CMD_GET_LED_MAP: {
            qmkd_build_lookup();

            uint8_t start = data[1];
            uint8_t count = 0;

            for (uint8_t i = start;
                 i < RGB_MATRIX_LED_COUNT && count < QMKD_LEDS_PER_MAP_PKT;
                 i++, count++)
            {
                uint8_t off = 3 + count * 7;
                response[off + 0] = g_led_config.point[i].x;
                response[off + 1] = g_led_config.point[i].y;
                response[off + 2] = g_led_config.flags[i];
                response[off + 3] = qmkd_led_row[i];
                response[off + 4] = qmkd_led_col[i];

                /* Report layer-0 keycode if this LED is in the matrix */
                uint16_t kc = 0;
                if (qmkd_led_row[i] != 0xFF) {
                    kc = keycode_at_keymap_location(0, qmkd_led_row[i], qmkd_led_col[i]);
                }
                response[off + 5] = (uint8_t)(kc & 0xFF);
                response[off + 6] = (uint8_t)((kc >> 8) & 0xFF);
            }

            response[0] = QMKD_CMD_GET_LED_MAP;
            response[1] = start;
            response[2] = count;
            return true;
        }

        default:
            return false;
    }
}

/*---------------------------------------------------------*\
| qmkd_rgb_matrix_indicator                                 |
|                                                           |
|   Call from rgb_matrix_indicators_advanced_user() to      |
|   override LEDs when host control is active.              |
|                                                           |
|   Returns true if host control is active (caller should   |
|   skip its own effects), false otherwise.                 |
\*---------------------------------------------------------*/
static bool qmkd_rgb_matrix_indicator(uint8_t led_min, uint8_t led_max) {
    if (!qmkd_active) return false;

    /* Auto-disable if heartbeat timeout exceeded */
    if (timer_elapsed32(qmkd_last_heartbeat) > QMKD_TIMEOUT_MS) {
        qmkd_active = false;
        return false;
    }

    for (uint8_t i = led_min; i < led_max; i++) {
        rgb_matrix_set_color(i,
            qmkd_colors[i][0],
            qmkd_colors[i][1],
            qmkd_colors[i][2]);
    }
    return true;
}
