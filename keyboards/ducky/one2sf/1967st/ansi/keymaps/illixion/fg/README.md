# fg/ — FocusGuard host protocol

Firmware side of the Mac companion protocol (FocusGuard, `kbled`): per-key LED
overrides and the encrypted keystroke stream. Full protocol and threat model:
[`../docs/PROTOCOL.md`](../docs/PROTOCOL.md).

* `fg_proto.h`   — command / status constants (shared with the host tools)
* `fg_secure.*`  — ChaCha20-Poly1305 (RFC 8439) on top of Monocypher; the key comes from
  `fg_device_key.h`, which only `../tools/fg-flash.sh` creates, in a temporary directory
* `fg_stream.*`  — authenticated, deadman-guarded, encrypted key stream; native key
  suppression while open, Esc cancel + cooldown
* `fg_leds.*`    — LED override layer rendered after every animation
* `fg_cmd.*`     — raw-HID dispatcher for commands 0xB0..0xBF
* `monocypher.*` — Monocypher 4.0.2, unmodified (BSD-2-Clause OR CC0-1.0),
  tarball SHA-256 `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c`
