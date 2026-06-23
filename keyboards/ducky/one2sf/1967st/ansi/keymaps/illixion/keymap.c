#include QMK_KEYBOARD_H
#include "os_detection.h"
#include "qmk_openrgb_direct.h"

void last_matrix_activity_trigger(void);

// --- Custom keycodes ---
enum custom_keycodes {
    CMD_ESC_GRV = SAFE_RANGE
};

// --- QMK Direct Protocol (QMKD) ---
// Universal OpenRGB support via qmk_openrgb_direct.h
// See protocol spec in that header for details.

void raw_hid_receive(uint8_t *data, uint8_t length) {
    uint8_t response[RAW_EPSIZE];
    memset(response, 0, RAW_EPSIZE);

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

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    if (qmkd_rgb_matrix_indicator(led_min, led_max)) {
        return true;  // Host is controlling LEDs — skip local effects
    }
    return false;
}

// --- Suspend: turn off LEDs when host sleeps ---
void suspend_power_down_user(void) {
    rgb_matrix_set_suspend_state(true);
}

void suspend_wakeup_init_user(void) {
    rgb_matrix_set_suspend_state(false);
}

// --- OS Detection: auto-swap Alt/GUI ---
bool process_detected_host_os_user(os_variant_t detected_os) {
    switch (detected_os) {
        case OS_MACOS:
        case OS_IOS:
            // Native layout: Ctrl, Alt(Option), GUI(Cmd) - no swap needed
            keymap_config.swap_lalt_lgui = false;
            keymap_config.swap_ralt_rgui = false;
            break;
        case OS_WINDOWS:
        case OS_LINUX:
            // Swap so physical keys become: Ctrl, Win/Super, Alt
            keymap_config.swap_lalt_lgui = true;
            keymap_config.swap_ralt_rgui = true;
            break;
        case OS_UNSURE:
            break;
    }
    return true;
}

// --- Esc/Grave toggle ---
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case CMD_ESC_GRV:
            if (record->event.pressed) {
                if (get_mods() & MOD_MASK_GUI) {
                    tap_code16(KC_GRV);
                } else {
                    tap_code16(KC_ESC);
                }
                return false;
            }
            break;
    }
    return true;
}


const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
	[0] = LAYOUT_ansi(CMD_ESC_GRV, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS, KC_EQL, KC_BSPC, KC_DEL, KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_BSLS, KC_PGUP, MO(1), KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT, KC_ENT, KC_PGDN, KC_LSFT, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_RSFT, KC_UP, KC_LCTL, KC_LALT, KC_LGUI, KC_SPC, KC_RGUI, MO(1), KC_RALT, KC_LEFT, KC_DOWN, KC_RGHT),
	[1] = LAYOUT_ansi(KC_GRV, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_DEL, KC_TRNS, KC_CAPS, KC_HOME, KC_UP, KC_END, KC_PGUP, KC_VOLU, KC_INS, MS_BTN1, MS_UP, MS_BTN2, MS_WHLU, KC_HOME, KC_PSCR, KC_TRNS, KC_HOME, KC_TRNS, KC_LEFT, KC_DOWN, KC_RGHT, KC_PGDN, KC_VOLD, KC_SCRL, MS_LEFT, MS_DOWN, MS_RGHT, MS_WHLD, KC_END, KC_TRNS, KC_END, KC_TRNS, KC_MPLY, KC_VOLD, KC_VOLU, KC_MPRV, KC_MNXT, RM_SATD, KC_MUTE, KC_VOLD, KC_VOLU, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, MO(2), KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS),
	[2] = LAYOUT_ansi(KC_NUM, KC_P1, KC_P2, KC_P3, KC_P4, KC_P5, KC_P6, KC_P7, KC_P8, KC_P9, KC_P0, KC_PMNS, KC_PPLS, KC_TRNS, EE_CLR, KC_CAPS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, AG_TOGG, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, RM_TOGG, RM_NEXT, RM_SPDD, RM_SPDU, RM_SATD, RM_SATU, KC_TRNS, KC_TRNS, KC_PDOT, KC_TRNS, KC_TRNS, RM_VALU, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, RM_HUED, RM_VALD, RM_HUEU)
};

#if defined(ENCODER_ENABLE) && defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {

};
#endif // defined(ENCODER_ENABLE) && defined(ENCODER_MAP_ENABLE)
