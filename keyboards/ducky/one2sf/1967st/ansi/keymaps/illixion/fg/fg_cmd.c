// fg_cmd.c — see fg_cmd.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fg_cmd.h"

#include <string.h>

#include "quantum.h"

#include "fg_leds.h"
#include "fg_proto.h"
#include "fg_secure.h"
#include "fg_stream.h"

#define LED_SET_MAX 7   // 4 header bytes + 7 * 4 = 32

bool fg_process_packet(const uint8_t *data, uint8_t length, uint8_t *resp) {
    if (length < 1 || data[0] < 0xB0 || data[0] > 0xBF) return false;
    memset(resp, 0, 32);
    resp[0] = data[0];
    if (length < 32) { resp[1] = FG_ST_BAD_ARGS; return true; }   // QMK always delivers 32

    switch (data[0]) {
        case FG_CMD_STATUS: {
            uint64_t ctr = fg_stream_last_counter();
            resp[1] = FG_ST_OK;
            resp[2] = FG_PROTO_VERSION;
            resp[3] = (fg_key_provisioned() ? FG_FLAG_KEY_PROVISIONED : 0) |
                      (fg_stream_active() ? FG_FLAG_STREAM_ACTIVE : 0);
            resp[4] = fg_leds_total();
            resp[5] = fg_leds_count();
            for (int i = 0; i < 6; i++) resp[6 + i] = (uint8_t)(ctr >> (8 * i));
            break;
        }
        case FG_CMD_LED_SET: {
            // [cmd][ttl_hi][ttl_lo][n]{led r g b}*n
            uint8_t n = data[3];
            if (length < 4 || n == 0 || n > LED_SET_MAX || length < 4 + 4 * n) { resp[1] = FG_ST_BAD_ARGS; break; }
            uint16_t ttl = ((uint16_t)data[1] << 8) | data[2];
            uint8_t applied = 0;
            for (uint8_t i = 0; i < n; i++) {
                const uint8_t *e = data + 4 + 4 * i;
                if (fg_leds_set(e[0], e[1], e[2], e[3], ttl)) applied++;
            }
            resp[1] = applied == n ? FG_ST_OK : FG_ST_BAD_ARGS;
            resp[2] = applied;
            break;
        }
        case FG_CMD_LED_RESET: {
            // [cmd][n]{led}*n   (n == 0 resets everything)
            uint8_t n = data[1];
            if (n > 30 || length < 2 + n) { resp[1] = FG_ST_BAD_ARGS; break; }
            if (n == 0) fg_leds_reset_all();
            else for (uint8_t i = 0; i < n; i++) fg_leds_reset(data[2 + i]);
            resp[1] = FG_ST_OK;
            break;
        }
        case FG_CMD_LED_GET: {
            uint8_t rgb[3];
            uint16_t ttl = 0;
            bool on = fg_leds_get(data[1], rgb, &ttl);
            resp[1] = data[1] < fg_leds_total() ? FG_ST_OK : FG_ST_BAD_ARGS;
            resp[2] = data[1];
            resp[3] = on ? 1 : 0;
            if (on) { resp[4] = rgb[0]; resp[5] = rgb[1]; resp[6] = rgb[2]; resp[7] = ttl >> 8; resp[8] = (uint8_t)ttl; }
            break;
        }
        case FG_CMD_STREAM_START:
        case FG_CMD_STREAM_PING:
        case FG_CMD_STREAM_STOP:
            fg_stream_handle(data, resp);
            break;
        default:
            resp[1] = FG_ST_BAD_ARGS;
    }
    return true;
}
