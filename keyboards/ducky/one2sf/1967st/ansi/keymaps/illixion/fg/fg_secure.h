// fg_secure.h — ChaCha20-Poly1305 (RFC 8439) with the provisioned device key.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// True when this firmware build contains a key (built by tools/fg-flash.sh).
bool fg_key_provisioned(void);

// Encrypt `len` bytes of `pt` into `ct` and produce the tag. `ad` is authenticated, not encrypted.
void fg_aead_seal(const uint8_t nonce[12], const uint8_t *ad, size_t ad_len,
                  const uint8_t *pt, size_t len, uint8_t *ct, uint8_t tag[16]);

// Tag-only authentication of `msg` (empty plaintext). Returns true when the tag matches.
bool fg_mac_verify(const uint8_t nonce[12], const uint8_t *msg, size_t len, const uint8_t tag[16]);
