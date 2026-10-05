#include QMK_KEYBOARD_H
#include "os_detection.h"
#include "qmk_openrgb_direct.h"
#include "fg/fg_cmd.h"
#include "fg/fg_leds.h"
#include "fg/fg_proto.h"
#include "fg/fg_stream.h"

void last_matrix_activity_trigger(void);

// --- Custom keycodes ---
enum custom_keycodes {
    CMD_ESC_GRV = SAFE_RANGE,
    MACWIN_TOGG,  // Manual Mac<->Windows layout toggle (replaces AG_TOGG)
    PASSTHRU_TOGG // Fn1+Backspace: toggle visionOS keyboard passthrough (host-side mode)
};

// --- Host-authoritative layout override (raw HID) ---
// QMK's USB OS detection mis-fingerprints macOS as Linux on this board because
// macOS serves cached string descriptors (no 0x02 probes -> looks like Linux).
// Instead of trusting the keyboard's guess, the Mac asserts the layout over raw
// HID at device-attach time (launchd com.apple.iokit.matching). Command byte is
// kept out of the QMKD command range (0x01-0x07, 0xFF) to avoid collisions.
//   0xA1 <1=mac|0=win>
#define HOSTCMD_SET_LAYOUT 0xA1

// --- Privacy blackout (raw HID) ---
// macOS engages "secure input" when a password field is focused. The host
// daemon watches that state and tells the keyboard to black out all LEDs while
// it's active, so reactive/heatmap RGB effects can't reveal which keys are
// pressed (e.g. typing a password in public).
//   0xA2 <1=blackout|0=normal>
#define HOSTCMD_SET_PRIVACY 0xA2

// --- Secure keystroke stream + LED overrides (raw HID, see fg/ and docs/PROTOCOL.md) ---
// The old unauthenticated keystroke mirror (0xA3) and native-report suppression
// (0xA4) are gone: any local process could enable the mirror and read the keys
// without Input Monitoring, which defeated macOS secure input. Keys now leave the
// board only inside an authenticated, encrypted, dead-man-guarded stream that the
// Mac enables solely while it cannot read the keyboard any other way.
// --- visionOS passthrough toggle (raw HID, keyboard -> host) ---
// Fn1+Backspace emits this so the host flips a "passthrough" mode: stop
// suppressing/injecting and let UC route the keyboard to visionOS as normal.
// Emitted (not a HOSTCMD); the host owns the mode state and toggles on each.
//   EMITTED:  0xEF
#define PASSTHRU_TOGGLE_REPORT 0xEF

// --- Passthrough LED indicator (raw HID, host -> keyboard) ---
// Host asserts whether passthrough mode is active so the Backspace key can show
// a white override. Host-driven because passthrough state lives on the host.
//   HOSTCMD:  0xA6 <1=on|0=off>
#define HOSTCMD_SET_PASSTHRU_LED 0xA6

// --- Mac/Windows layout state ---
// Single source of truth for the Alt/GUI swap. We track it explicitly instead
// of reading keymap_config so the toggle never "assumes" a state that OS
// detection may have changed behind the user's back.
//   mac_layout == true  -> Mac:     Ctrl, Alt(Option), GUI(Cmd), no swap
//   mac_layout == false -> Windows: swap Alt<->GUI so keys read Ctrl, Win, Alt
static bool mac_layout = true;   // default to Mac
static bool layout_locked = false; // set once the user toggles manually

// --- Privacy blackout state ---
// When true, all LEDs are forced off every frame, overriding both local effects
// and host/OpenRGB frames. Driven by the host daemon from macOS secure-input.
static bool privacy_blackout = false;

// --- Passthrough LED state ---
// Host-driven mirror of passthrough mode, for the Backspace white indicator.
static bool passthru_led_on = false;

static void apply_layout(bool mac) {
    mac_layout = mac;
    // NB: applied in-memory only (no eeconfig_update_keymap). Persisting the
    // swap is what caused the keyboard to boot into a stale Windows layout, so
    // we deliberately re-assert the default on every boot instead.
    keymap_config.swap_lalt_lgui = !mac;
    keymap_config.swap_ralt_rgui = !mac;
}

// --- QMK Direct Protocol (QMKD) ---
// Universal OpenRGB support via qmk_openrgb_direct.h
// See protocol spec in that header for details.

void raw_hid_receive(uint8_t *data, uint8_t length) {
    uint8_t response[RAW_EPSIZE];
    memset(response, 0, RAW_EPSIZE);

    // Host-authoritative layout override — handled before QMKD so it can't be
    // shadowed by the LED protocol. The host's decision is final: lock out OS
    // detection so the (wrong) async guess can't flip it back afterwards.
    if (data[0] == HOSTCMD_SET_LAYOUT) {
        bool mac = (data[1] != 0);
        layout_locked = true;
        apply_layout(mac);
        response[0] = HOSTCMD_SET_LAYOUT;
        response[1] = mac ? 1 : 0;
        raw_hid_send(response, RAW_EPSIZE);
        return;
    }

    // Privacy blackout toggle — also handled before QMKD so a password-entry
    // blackout can't be undone by an in-flight OpenRGB frame.
    if (data[0] == HOSTCMD_SET_PRIVACY) {
        privacy_blackout = (data[1] != 0);
        last_matrix_activity_trigger();  // wake the matrix so the change renders now
        response[0] = HOSTCMD_SET_PRIVACY;
        response[1] = privacy_blackout ? 1 : 0;
        raw_hid_send(response, RAW_EPSIZE);
        return;
    }

    // FocusGuard protocol (0xB0..0xBF): LED overrides and the secure stream.
    if (fg_process_packet(data, length, response)) {
        raw_hid_send(response, RAW_EPSIZE);
        return;
    }

    // Passthrough LED indicator — host tells us to light the Backspace key.
    if (data[0] == HOSTCMD_SET_PASSTHRU_LED) {
        passthru_led_on = (data[1] != 0);
        response[0] = HOSTCMD_SET_PASSTHRU_LED;
        response[1] = passthru_led_on ? 1 : 0;
        raw_hid_send(response, RAW_EPSIZE);
        return;
    }

    if (!qmkd_process_packet(data, length, response)) {
        response[0] = QMKD_RSP_UNKNOWN;
    }

    // Treat host LED traffic as activity so RGB_MATRIX_TIMEOUT doesn't
    // suspend the matrix while Aurora/OpenRGB is actively driving it.
    // Without this, the indicator callback stops running once suspended
    // and host frames silently don't render until a key is pressed.
    if (data[0] == QMKD_CMD_SET_LEDS ||
        data[0] == QMKD_CMD_ENABLE   ||
        data[0] == QMKD_CMD_HEARTBEAT) {
        last_matrix_activity_trigger();
    }

    raw_hid_send(response, RAW_EPSIZE);
}

// LED indices for the status-indicator keys, resolved once from the base-layer
// keycode so we don't hardcode board-specific numbers: Caps Lock indicator sits
// on the Tab key (Fn1+Tab toggles caps), passthrough on Backspace (Fn1+Bksp).
static uint8_t caps_led_index = NO_LED;
static uint8_t passthru_led_index = NO_LED;
static bool indicator_leds_resolved = false;

static void resolve_indicator_leds(void) {
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint8_t led = g_led_config.matrix_co[row][col];
            if (led == NO_LED) continue;
            uint16_t kc = keymap_key_to_keycode(0, (keypos_t){.row = row, .col = col});
            if (kc == KC_TAB)  caps_led_index = led;
            if (kc == KC_BSPC) passthru_led_index = led;
        }
    }
    indicator_leds_resolved = true;
}

// White override for a single key, honoring the current dirty LED range.
static void set_indicator_white(uint8_t led, uint8_t led_min, uint8_t led_max) {
    if (led != NO_LED && led >= led_min && led < led_max) {
        rgb_matrix_set_color(led, 255, 255, 255);
    }
}

// While the secure keystroke stream is open the whole board pulses red and Esc (the key
// that cancels it) glows solid red. Drawn even under privacy blackout and after every
// override, so malware can neither hide nor fake it.
#define STREAM_PULSE_PERIOD_MS 1600
static uint8_t esc_led_index = NO_LED;
static void draw_stream_indicator(uint8_t led_min, uint8_t led_max) {
    if (!fg_stream_active()) return;
    if (esc_led_index == NO_LED) esc_led_index = g_led_config.matrix_co[0][0];
    uint16_t phase = timer_read() % STREAM_PULSE_PERIOD_MS;
    uint16_t half  = STREAM_PULSE_PERIOD_MS / 2;
    uint16_t ramp  = phase < half ? phase : STREAM_PULSE_PERIOD_MS - phase;   // triangle 0..half
    uint8_t  level = 30 + (uint8_t)((uint32_t)ramp * 170 / half);            // 30..200, never dark
    for (uint8_t i = led_min; i < led_max; i++) {
        rgb_matrix_set_color(i, i == esc_led_index ? 255 : level, 0, 0);
    }
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    // Privacy first: when macOS secure input is active, force every LED off and
    // skip both host frames and local effects so keypresses can't be revealed.
    if (privacy_blackout) {
        for (uint8_t i = led_min; i < led_max; i++) {
            rgb_matrix_set_color(i, 0, 0, 0);
        }
        draw_stream_indicator(led_min, led_max);   // the one thing that must never be hidden
        return true;
    }

    bool host_controlled = qmkd_rgb_matrix_indicator(led_min, led_max);

    // Status overlays render LAST so they override both local animations and host
    // (OpenRGB) frames: Caps Lock (from the OS LED report) and passthrough mode.
    if (!indicator_leds_resolved) resolve_indicator_leds();
    if (host_keyboard_led_state().caps_lock) set_indicator_white(caps_led_index, led_min, led_max);
    if (passthru_led_on)                     set_indicator_white(passthru_led_index, led_min, led_max);

    // Mac-pinned key colours (kbled / status tracking): above every animation and
    // OpenRGB frame, below the Caps/passthrough whites so those stay trustworthy.
    fg_leds_render(led_min, led_max);
    // Last of all, and not overridable: a red Esc while keystrokes are being mirrored.
    draw_stream_indicator(led_min, led_max);

    return host_controlled;
}

// --- Suspend: turn off LEDs when host sleeps ---
void suspend_power_down_user(void) {
    rgb_matrix_set_suspend_state(true);
}

void suspend_wakeup_init_user(void) {
    rgb_matrix_set_suspend_state(false);
}

// --- Default to Mac at boot ---
// EEPROM may hold a stale swap state, and OS detection only fires (async) once
// the host is confidently identified. Force the Mac default here so the board
// is always usable in Mac layout immediately on plug-in; detection may switch
// it shortly after.
void keyboard_post_init_user(void) {
    apply_layout(true);
}

// --- OS Detection: auto-swap Alt/GUI (unless the user has taken over) ---
// Kept as a best-effort fallback. On hosts where detection works it sets the
// layout; on this Mac it mis-guesses Linux, but the host-authoritative raw-HID
// command (HOSTCMD_SET_LAYOUT) sets layout_locked and overrides it.
bool process_detected_host_os_user(os_variant_t detected_os) {
    if (layout_locked) {
        return true;  // user pressed TOGG manually — their choice wins
    }
    switch (detected_os) {
        case OS_MACOS:
        case OS_IOS:
            apply_layout(true);   // native Mac layout, no swap
            break;
        case OS_WINDOWS:
        case OS_LINUX:
            apply_layout(false);  // swap Alt<->GUI for Win/Super
            break;
        case OS_UNSURE:
            break;
    }
    return true;
}

// --- Secure stream: which keys leave the board ---
// Plain keys and modifiers (everything the Mac must re-inject). Layers, RGB, mouse
// and custom keycodes (>0xFF) stay local: they are not typed characters.
static bool is_mirrorable_key(uint16_t keycode) {
    return keycode >= KC_A && keycode <= 0xFF;        // includes the modifiers
}

// Dead-man timeout and duration cap for the secure stream.
void housekeeping_task_user(void) {
    fg_stream_task();
    // Keep the matrix awake so RGB_MATRIX_TIMEOUT can't blank the red pulse.
    if (fg_stream_active()) last_matrix_activity_trigger();
}

// --- Esc/Grave toggle ---
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Nothing is emitted unless the Mac has an authenticated stream open. While one is,
    // typed keys go only into the stream (modifiers stay native: they type nothing, and
    // holding them back is what used to leave modifiers stuck).
    if (is_mirrorable_key(keycode)) {
        fg_stream_emit((uint8_t)keycode, record->event.pressed, get_mods());
        if (!IS_MODIFIER_KEYCODE(keycode) && fg_stream_suppress((uint8_t)keycode, record->event.pressed)) {
            return false;
        }
    }

    switch (keycode) {
        case CMD_ESC_GRV:
            if (record->event.pressed) {
                // GUI -> grave (Cmd+`), Shift -> grave (which the held Shift
                // turns into ~), otherwise Esc. Mirrors the Fn1 layer's KC_GRV.
                uint8_t out = (get_mods() & (MOD_MASK_GUI | MOD_MASK_SHIFT)) ? KC_GRV : KC_ESC;
                // tap_code16() does not pass through this function again, so the secure
                // stream never sees the tap: relay it by hand (Esc cancels a pinentry prompt).
                if (fg_stream_active()) {
                    fg_stream_emit(out, true, get_mods());
                    fg_stream_emit(out, false, get_mods());
                    // Esc also ends the stream, whoever opened it. Nothing goes out natively
                    // while it is open.
                    if (out == KC_ESC) fg_stream_cancel();
                    return false;
                }
                tap_code16(out);
                return false;
            }
            break;
        case MACWIN_TOGG:
            if (record->event.pressed) {
                // Deterministic flip from our own tracked state — never reads
                // keymap_config, so it can't be desynced by OS detection.
                layout_locked = true;
                apply_layout(!mac_layout);
            }
            return false;
        case PASSTHRU_TOGG:
            if (record->event.pressed) {
                // Tell the Mac bridge to toggle visionOS passthrough mode. The
                // host owns the state; we just signal the edge. Sent on a custom
                // keycode (>0xFF) so it's never mirrored.
                uint8_t report[RAW_EPSIZE];
                memset(report, 0, RAW_EPSIZE);
                report[0] = PASSTHRU_TOGGLE_REPORT;
                raw_hid_send(report, RAW_EPSIZE);
            }
            return false;
    }
    return true;
}


const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
	[0] = LAYOUT_ansi(CMD_ESC_GRV, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS, KC_EQL, KC_BSPC, KC_DEL, KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_BSLS, KC_PGUP, MO(1), KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT, KC_ENT, KC_PGDN, KC_LSFT, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_RSFT, KC_UP, KC_LCTL, KC_LALT, KC_LGUI, KC_SPC, KC_RGUI, MO(1), KC_RALT, KC_LEFT, KC_DOWN, KC_RGHT),
	[1] = LAYOUT_ansi(KC_GRV, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, PASSTHRU_TOGG, KC_TRNS, KC_CAPS, KC_HOME, KC_UP, KC_END, KC_PGUP, KC_VOLU, KC_INS, MS_BTN1, MS_UP, MS_BTN2, MS_WHLU, KC_HOME, KC_PSCR, KC_TRNS, KC_HOME, KC_TRNS, KC_LEFT, KC_DOWN, KC_RGHT, KC_PGDN, KC_VOLD, KC_SCRL, MS_LEFT, MS_DOWN, MS_RGHT, MS_WHLD, KC_END, KC_TRNS, KC_END, KC_TRNS, KC_MPLY, KC_VOLD, KC_VOLU, KC_MPRV, KC_MNXT, RM_SATD, KC_MUTE, KC_VOLD, KC_VOLU, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, MO(2), KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS),
	[2] = LAYOUT_ansi(KC_NUM, KC_P1, KC_P2, KC_P3, KC_P4, KC_P5, KC_P6, KC_P7, KC_P8, KC_P9, KC_P0, KC_PMNS, KC_PPLS, KC_TRNS, EE_CLR, KC_CAPS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, MACWIN_TOGG, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, RM_TOGG, RM_NEXT, RM_SPDD, RM_SPDU, RM_SATD, RM_SATU, KC_TRNS, KC_TRNS, KC_PDOT, KC_TRNS, KC_TRNS, RM_VALU, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, RM_HUED, RM_VALD, RM_HUEU)
};

#if defined(ENCODER_ENABLE) && defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {

};
#endif // defined(ENCODER_ENABLE) && defined(ENCODER_MAP_ENABLE)
