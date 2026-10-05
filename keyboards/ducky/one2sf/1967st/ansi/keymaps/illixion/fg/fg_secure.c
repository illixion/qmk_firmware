// fg_secure.c — RFC 8439 AEAD built from Monocypher's ChaCha20 and Poly1305.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The 256-bit key lives only in the Mac's login Keychain. tools/fg-flash.sh reads it at
// build time into a header in a private temporary directory (passed as FG_KEY_DIR), builds
// there, flashes and deletes it all, so no copy of the key — header, object file or image —
// stays on disk. It never travels over USB and no command can read it. A build without
// FG_KEY_DIR still succeeds; every secure feature then reports FG_ST_UNAVAILABLE.
#include "fg_secure.h"

#include "monocypher.h"

// Older builds kept the key in this file. It must not linger: anything running as you could read it.
#if __has_include("fg_secret.h")
#    error "fg/fg_secret.h holds a device key on disk: delete it (the key now lives only in the Keychain; build with tools/fg-flash.sh)"
#endif

// Angle brackets: searched only on the -I path from FG_KEY_DIR, never next to this file.
#if __has_include(<fg_device_key.h>)
#    include <fg_device_key.h>      // #define FG_SECRET_KEY_BYTES { 0x.., ... 32 bytes }
#    define FG_HAVE_KEY 1
static const uint8_t fg_key[32] = FG_SECRET_KEY_BYTES;
#else
#    define FG_HAVE_KEY 0
#endif

bool fg_key_provisioned(void) { return FG_HAVE_KEY; }

#if FG_HAVE_KEY

static void store64_le(uint8_t *out, uint64_t v) {
    for (int i = 0; i < 8; i++) out[i] = (uint8_t)(v >> (8 * i));
}

static void poly_pad16(crypto_poly1305_ctx *ctx, size_t len) {
    static const uint8_t zeros[16] = {0};
    if (len % 16) crypto_poly1305_update(ctx, zeros, 16 - (len % 16));
}

// tag = Poly1305(otk, ad || pad || ct || pad || len(ad) || len(ct)), otk = ChaCha20 block 0
static void aead_tag(uint8_t tag[16], const uint8_t nonce[12], const uint8_t *ad, size_t ad_len,
                     const uint8_t *ct, size_t ct_len) {
    uint8_t otk[32];
    crypto_chacha20_ietf(otk, NULL, sizeof otk, fg_key, nonce, 0);
    crypto_poly1305_ctx ctx;
    crypto_poly1305_init(&ctx, otk);
    crypto_poly1305_update(&ctx, ad, ad_len);
    poly_pad16(&ctx, ad_len);
    if (ct_len) crypto_poly1305_update(&ctx, ct, ct_len);
    poly_pad16(&ctx, ct_len);
    uint8_t lens[16];
    store64_le(lens, ad_len);
    store64_le(lens + 8, ct_len);
    crypto_poly1305_update(&ctx, lens, sizeof lens);
    crypto_poly1305_final(&ctx, tag);
    crypto_wipe(otk, sizeof otk);
    crypto_wipe(&ctx, sizeof ctx);
}

void fg_aead_seal(const uint8_t nonce[12], const uint8_t *ad, size_t ad_len,
                  const uint8_t *pt, size_t len, uint8_t *ct, uint8_t tag[16]) {
    crypto_chacha20_ietf(ct, pt, len, fg_key, nonce, 1);   // block 0 is reserved for the Poly1305 key
    aead_tag(tag, nonce, ad, ad_len, ct, len);
}

bool fg_mac_verify(const uint8_t nonce[12], const uint8_t *msg, size_t len, const uint8_t tag[16]) {
    uint8_t expect[16];
    aead_tag(expect, nonce, msg, len, NULL, 0);
    bool ok = crypto_verify16(expect, tag) == 0;          // constant time
    crypto_wipe(expect, sizeof expect);
    return ok;
}

#else   // no key in this build

void fg_aead_seal(const uint8_t nonce[12], const uint8_t *ad, size_t ad_len,
                  const uint8_t *pt, size_t len, uint8_t *ct, uint8_t tag[16]) {
    (void)nonce; (void)ad; (void)ad_len; (void)pt; (void)len; (void)ct; (void)tag;
}

bool fg_mac_verify(const uint8_t nonce[12], const uint8_t *msg, size_t len, const uint8_t tag[16]) {
    (void)nonce; (void)msg; (void)len; (void)tag;
    return false;
}

#endif
