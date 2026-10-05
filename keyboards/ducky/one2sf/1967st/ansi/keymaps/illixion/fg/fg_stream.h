// fg_stream.h — authenticated, encrypted keystroke stream (secure-input bypass).
// SPDX-License-Identifier: GPL-2.0-or-later
//
// macOS hides keystrokes from every normal reader while a password field has
// Secure Event Input on, which also blinds the Mac-side bridge. This stream is
// the one path around that, so it is locked down: authenticated start, encrypted
// frames, a dead-man timeout, a hard duration cap, native key suppression (an open
// stream stops normal typing), Esc to cancel, and an unmissable red pulse.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Handle FG_CMD_STREAM_START / PING / STOP. `resp` is the 32-byte response buffer.
void fg_stream_handle(const uint8_t *data, uint8_t *resp);
bool fg_stream_active(void);
// Queue one key event for the host (no-op unless a stream is active).
void fg_stream_emit(uint8_t usage, bool pressed, uint8_t mods);
// The user pressed Esc: end the stream now and refuse new ones for FG_STREAM_COOLDOWN_MS.
void fg_stream_cancel(void);
// While a stream is open, typed keys reach only the stream, never the host's keyboard
// input, so a stream nobody asked for is impossible to miss: typing stops working.
// Returns true when this key event must not be sent to the host as a normal key.
bool fg_stream_suppress(uint8_t keycode, bool pressed);
// Call from housekeeping: enforces the dead-man timeout and the duration cap.
void fg_stream_task(void);
uint64_t fg_stream_last_counter(void);
