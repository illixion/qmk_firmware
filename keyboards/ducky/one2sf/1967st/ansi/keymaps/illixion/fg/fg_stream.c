// fg_stream.c — see fg_stream.h and docs/PROTOCOL.md.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fg_stream.h"

#include <string.h>

#include "quantum.h"
#include "raw_hid.h"
#include "usb_descriptor.h"

#include "fg_proto.h"
#include "fg_secure.h"

static bool     active;
static uint8_t  session[8];
static uint32_t frame_ctr;         // 24-bit on the wire
static uint32_t last_ping;
static uint32_t started_at;
static uint64_t last_ctr;          // highest command counter ever accepted this boot
static uint8_t  recent_sessions[4][8];
static uint8_t  recent_next;
static bool     cooling;           // an Esc cancel happened; refuse starts until cooldown_from + COOLDOWN
static uint32_t cooldown_from;
static uint8_t  suppressed[32];    // bitmap of basic keycodes whose press was kept from the host

uint64_t fg_stream_last_counter(void) { return last_ctr; }
bool     fg_stream_active(void)       { return active; }

static uint64_t load48(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 5; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

// Nonce for command MACs: domain | 3 zero | counter (6 bytes LE) | 2 zero.
static void cmd_nonce(uint8_t nonce[12], const uint8_t *ctr6) {
    memset(nonce, 0, 12);
    nonce[0] = FG_NONCE_CMD;
    memcpy(nonce + 4, ctr6, 6);
}

// Verify tag over data[0 .. msg_len) with the counter at data[1..6]; enforce monotonic counter.
static uint8_t authenticate(const uint8_t *data, uint8_t msg_len) {
    if (!fg_key_provisioned()) return FG_ST_UNAVAILABLE;
    uint8_t nonce[12];
    cmd_nonce(nonce, data + 1);
    if (!fg_mac_verify(nonce, data, msg_len, data + msg_len)) return FG_ST_AUTH_FAILED;
    uint64_t ctr = load48(data + 1);
    if (ctr <= last_ctr) return FG_ST_REPLAY;
    last_ctr = ctr;          // only after the tag verified, so forged packets can't move it
    return FG_ST_OK;
}

static void end_stream(uint8_t reason, bool notify) {
    if (!active) return;
    active = false;
    memset(session, 0, sizeof session);
    if (notify) {
        uint8_t evt[RAW_EPSIZE] = {0};
        evt[0] = FG_EVT_STREAM_ENDED;
        evt[1] = reason;
        raw_hid_send(evt, RAW_EPSIZE);
    }
}

void fg_stream_handle(const uint8_t *data, uint8_t *resp) {
    resp[0] = data[0];
    switch (data[0]) {
        case FG_CMD_STREAM_START: {
            // [cmd][ctr 6][session 8][tag 16]
            uint8_t st = authenticate(data, 15);
            if (st == FG_ST_OK && cooling) {
                if (timer_read32() - cooldown_from < FG_STREAM_COOLDOWN_MS) st = FG_ST_COOLDOWN;
                else cooling = false;
            }
            if (st == FG_ST_OK) {
                // Never reuse a session id: (key, session, frame counter) must be unique forever.
                for (uint8_t i = 0; i < 4; i++) {
                    if (memcmp(recent_sessions[i], data + 7, 8) == 0) { st = FG_ST_REPLAY; break; }
                }
            }
            if (st == FG_ST_OK) {
                memcpy(session, data + 7, 8);
                memcpy(recent_sessions[recent_next], session, 8);
                recent_next = (recent_next + 1) & 3;
                frame_ctr  = 0;
                last_ping  = timer_read32();
                started_at = last_ping;
                active     = true;
            }
            resp[1] = st;
            break;
        }
        case FG_CMD_STREAM_PING: {
            // [cmd][ctr 6][tag 16]
            uint8_t st = authenticate(data, 7);
            if (st == FG_ST_OK) {
                if (active) last_ping = timer_read32();
                else st = FG_ST_NOT_ACTIVE;
            }
            resp[1] = st;
            break;
        }
        case FG_CMD_STREAM_STOP:
            end_stream(FG_END_STOPPED, false);   // open on purpose: stopping only reduces exposure
            resp[1] = FG_ST_OK;
            break;
        default:
            resp[1] = FG_ST_BAD_ARGS;
    }
}

void fg_stream_emit(uint8_t usage, bool pressed, uint8_t mods) {
    if (!active) return;
    if (frame_ctr > 0xFFFFFE) { end_stream(FG_END_EXHAUSTED, true); return; }

    uint8_t report[RAW_EPSIZE] = {0};
    report[0] = FG_EVT_STREAM_FRAME;
    report[1] = (uint8_t)frame_ctr;
    report[2] = (uint8_t)(frame_ctr >> 8);
    report[3] = (uint8_t)(frame_ctr >> 16);

    uint8_t nonce[12];
    nonce[0] = FG_NONCE_STREAM;
    memcpy(nonce + 1, session, 8);
    memcpy(nonce + 9, report + 1, 3);

    const uint8_t pt[4] = {pressed ? 1 : 0, usage, mods, 0};
    fg_aead_seal(nonce, report, 4, pt, sizeof pt, report + 4, report + 8);   // ct[4] then tag[16]
    frame_ctr++;
    raw_hid_send(report, RAW_EPSIZE);
}

void fg_stream_cancel(void) {
    if (!active) return;
    end_stream(FG_END_CANCELLED, true);
    cooling       = true;
    cooldown_from = timer_read32();
}

bool fg_stream_suppress(uint8_t keycode, bool pressed) {
    uint8_t bit = (uint8_t)(1u << (keycode & 7));
    if (pressed) {
        if (!active) return false;
        suppressed[keycode >> 3] |= bit;
        return true;
    }
    // A release is kept back only if its press was; a key held from before the stream
    // (or released after it ended) is released normally, so nothing sticks on the Mac.
    if (!(suppressed[keycode >> 3] & bit)) return false;
    suppressed[keycode >> 3] &= (uint8_t)~bit;
    return true;
}

void fg_stream_task(void) {
    if (!active) return;
    uint32_t now = timer_read32();
    if (now - last_ping > FG_STREAM_TIMEOUT_MS)      end_stream(FG_END_TIMEOUT, true);
    else if (now - started_at > FG_STREAM_MAX_MS)    end_stream(FG_END_MAX_DURATION, true);
}
