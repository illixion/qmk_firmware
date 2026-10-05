// fg_cmd.h — raw-HID dispatcher for the FocusGuard protocol (commands 0xB0..0xBF).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Returns true if `data` was a protocol command and `resp` (32 bytes) is filled in.
bool fg_process_packet(const uint8_t *data, uint8_t length, uint8_t *resp);
