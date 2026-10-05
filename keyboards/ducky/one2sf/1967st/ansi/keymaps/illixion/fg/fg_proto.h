// fg_proto.h — wire constants for the FocusGuard / kbled host protocol (v1).
// SPDX-License-Identifier: GPL-2.0-or-later
//
// 32-byte raw HID packets (usage page 0xFF60, usage 0x61). Byte 0 is the
// command. Commands marked AUTH carry a 128-bit ChaCha20-Poly1305 tag and a
// strictly increasing 48-bit counter; see docs/PROTOCOL.md.
#pragma once

#include <stdint.h>

#define FG_PROTO_VERSION 1

// host -> device
#define FG_CMD_STATUS       0xB0  // (open)  -> version, flags, counts, last accepted counter
#define FG_CMD_LED_SET      0xB1  // (open)  ttl_hi ttl_lo n {led r g b}*n   (n <= 7)
#define FG_CMD_LED_RESET    0xB2  // (open)  n {led}*n                       (n == 0 -> all)
#define FG_CMD_LED_GET      0xB3  // (open)  led
#define FG_CMD_STREAM_START 0xB8  // (AUTH)  ctr[6] session[8] tag[16]
#define FG_CMD_STREAM_PING  0xB9  // (AUTH)  ctr[6] tag[16]
#define FG_CMD_STREAM_STOP  0xBA  // (open)  stopping can only reduce exposure

// device -> host, unsolicited
#define FG_EVT_STREAM_FRAME 0xEC  // ctr[3] ciphertext[4] tag[16]
#define FG_EVT_STREAM_ENDED 0xED  // reason

// response status (byte 1 of every response)
#define FG_ST_OK            0
#define FG_ST_AUTH_FAILED   1
#define FG_ST_REPLAY        2
#define FG_ST_UNAVAILABLE   3     // no key provisioned in this firmware build
#define FG_ST_BAD_ARGS      4
#define FG_ST_NOT_ACTIVE    5
#define FG_ST_COOLDOWN      6     // Esc closed the last stream moments ago; try again later

// FG_EVT_STREAM_ENDED reasons
#define FG_END_TIMEOUT      1
#define FG_END_MAX_DURATION 2
#define FG_END_EXHAUSTED    3
#define FG_END_STOPPED      4
#define FG_END_CANCELLED    5     // the user pressed Esc on the keyboard

// STATUS flags
#define FG_FLAG_KEY_PROVISIONED 0x01
#define FG_FLAG_STREAM_ACTIVE   0x02

// Nonce domains (first byte of the 12-byte ChaCha20-Poly1305 nonce)
#define FG_NONCE_CMD    0x01
#define FG_NONCE_STREAM 0x02

#define FG_STREAM_TIMEOUT_MS 3000UL     // no authenticated ping for this long -> stream stops
#define FG_STREAM_MAX_MS     900000UL   // hard cap per session (15 min); host must start a new one
#define FG_STREAM_COOLDOWN_MS 10000UL   // after an Esc cancel, refuse new streams this long
