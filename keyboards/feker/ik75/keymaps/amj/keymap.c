/* Copyright 2022 Feker
 * Copyright 2022 HorrorTroll <https://github.com/HorrorTroll>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include QMK_KEYBOARD_H

#define VOLUME_INDICATOR_TIMEOUT_MS 25
#define MUTE_PLAY_HOLD_MS 200
#define CAPS_WORD_HOLD_MS 150
#define TURBO_INTERVAL_MS 50

#define LED_FN      31
#define LED_CAPS    32
#define LED_NUM     46
#define LED_MEDIA1  99
#define LED_MEDIA2 100
#define LED_MEDIA3 101
#define LED_STATUS 104

enum layer_names {
    _BASE,
    _FN,
};

enum custom_keycodes {
    MUTE_PLAY = SAFE_RANGE,
    CW_CAPS,
    SENT_CASE,
    STICKY_HOLD,
    TURBO_MODE,
};

typedef enum {
    VOL_INDICATOR_NONE,
    VOL_INDICATOR_UP,
    VOL_INDICATOR_DOWN,
} volume_indicator_t;

/* Sentence Case */
static bool     sentence_case_enabled = false;
static bool     capitalize_next       = false;
static uint16_t sentence_shifted_key  = KC_NO;

/* Caps Lock / Caps Word */
static uint16_t caps_timer = 0;

/* Volume and mute indicators */
static bool               mute_pressed     = false;
static volume_indicator_t volume_indicator = VOL_INDICATOR_NONE;
static uint16_t           volume_timer     = 0;

/* Mute / Play key */
static uint16_t mute_play_timer     = 0;
static bool     mute_play_pressed   = false;
static bool     mute_play_triggered = false;

/* Sticky Hold */
static bool     sticky_hold_armed  = false;
static bool     sticky_hold_active = false;
static uint16_t sticky_hold_key    = KC_NO;

/* Turbo */
static bool     turbo_armed  = false;
static bool     turbo_active = false;
static uint16_t turbo_key    = KC_NO;
static uint16_t turbo_timer  = 0;

/* Suppress releases for consumed physical key presses */
static uint16_t suppressed_release_key = KC_NO;

/* Indicator-only RGB mode */
static bool    indicator_only_mode = false;
static uint8_t saved_rgb_mode      = RGB_MATRIX_SOLID_COLOR;

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BASE] = LAYOUT(
        KC_ESC,  KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  KC_DEL,           MUTE_PLAY,
        KC_GRV,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,          KC_INS,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS,          KC_END,
        CW_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,           KC_PGUP,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH,          KC_RSFT, KC_UP,   KC_PGDN,
        KC_LCTL, KC_LGUI, KC_LALT,                            KC_SPC,                     KC_RALT, MO(_FN),           KC_RCTL, KC_LEFT, KC_DOWN, KC_RIGHT
    ),

    [_FN] = LAYOUT(
        _______, KC_MYCM, KC_WHOM, KC_CALC, KC_SLCT, KC_MPRV, KC_MNXT, KC_MPLY, KC_MSTP, KC_MUTE, KC_VOLD, KC_VOLU, KC_MAIL, _______,          RM_TOGG,
        NK_TOGG, KC_P1,   KC_P2,   KC_P3,   KC_P4,   KC_P5,   KC_P6,   KC_P7,   KC_P8,   KC_P9,   KC_P0,   RM_SPDD, RM_SPDU, _______,          RM_NEXT,
        _______, SENT_CASE, STICKY_HOLD, TURBO_MODE, _______, _______, _______, _______, _______, _______, KC_PSCR, _______, _______, _______, RM_PREV,
        _______, _______, KC_SCRL, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______,          RM_HUEU,
        _______,          _______, _______, _______, _______, _______, KC_NUM,  _______, _______, _______, _______,          _______, RM_VALU, RM_HUED,
        _______, GU_TOGG, _______,                            _______,                   _______, _______,          _______, RM_SPDD, RM_VALD, RM_SPDU
    ),
};

static void set_media_leds(uint8_t red, uint8_t green, uint8_t blue) {
    rgb_matrix_set_color(LED_MEDIA1, red, green, blue);
    rgb_matrix_set_color(LED_MEDIA2, red, green, blue);
    rgb_matrix_set_color(LED_MEDIA3, red, green, blue);
}

static void refresh_matrix(uint8_t led_flags) {
    rgb_matrix_set_flags(led_flags);
    rgb_matrix_set_color_all(0, 0, 0);
}

static void enter_indicator_only_mode(void) {
    if (!indicator_only_mode) {
        saved_rgb_mode = rgb_matrix_get_mode();
    }

    indicator_only_mode = true;
    rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
    refresh_matrix(LED_FLAG_NONE);
}

static void leave_indicator_only_mode(void) {
    indicator_only_mode = false;
    rgb_matrix_enable_noeeprom();
    rgb_matrix_mode_noeeprom(saved_rgb_mode);
    refresh_matrix(LED_FLAG_ALL);
}

static void stop_sticky_hold(void) {
    if (sticky_hold_active && sticky_hold_key != KC_NO) {
        unregister_code16(sticky_hold_key);
    }

    sticky_hold_key    = KC_NO;
    sticky_hold_active = false;
    sticky_hold_armed  = false;
}

static void stop_turbo(void) {
    turbo_key    = KC_NO;
    turbo_active = false;
    turbo_armed  = false;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    /*
     * Remove Sentence Case's temporary weak Shift immediately before
     * QMK processes the corresponding letter release.
     */
    if (!record->event.pressed && keycode == sentence_shifted_key) {
        sentence_shifted_key = KC_NO;
        del_weak_mods(MOD_BIT(KC_LSFT));
        send_keyboard_report();
        return true;
    }

    switch (keycode) {
        case SENT_CASE:
            if (record->event.pressed) {
                sentence_case_enabled = !sentence_case_enabled;
                capitalize_next       = sentence_case_enabled;

                if (!sentence_case_enabled &&
                    sentence_shifted_key != KC_NO) {
                    sentence_shifted_key = KC_NO;
                    del_weak_mods(MOD_BIT(KC_LSFT));
                    send_keyboard_report();
                }
            }
            return false;

        case STICKY_HOLD:
            if (record->event.pressed) {
                if (sticky_hold_active) {
                    stop_sticky_hold();
                } else {
                    sticky_hold_armed = !sticky_hold_armed;
                }

                turbo_armed = false;
            }
            return false;

        case TURBO_MODE:
            if (record->event.pressed) {
                if (turbo_active) {
                    stop_turbo();
                } else {
                    turbo_armed = !turbo_armed;
                }

                sticky_hold_armed = false;
            }
            return false;

        case MUTE_PLAY:
            if (record->event.pressed) {
                mute_play_pressed   = true;
                mute_play_triggered = false;
                mute_play_timer     = timer_read();
            } else {
                mute_play_pressed = false;

                if (!mute_play_triggered) {
                    tap_code16(KC_MUTE);
                    mute_pressed = !mute_pressed;
                }
            }
            return false;

        case CW_CAPS:
            if (record->event.pressed) {
                caps_timer = timer_read();
                return false;
            }

            if (timer_elapsed(caps_timer) >= CAPS_WORD_HOLD_MS) {
                caps_word_on();
            } else {
                tap_code16(KC_CAPS);
            }
            return false;

        case RM_TOGG:
            if (!record->event.pressed) {
                return false;
            }

            if (indicator_only_mode) {
                leave_indicator_only_mode();
                return false;
            }

            switch (rgb_matrix_get_flags()) {
                case LED_FLAG_ALL:
                    refresh_matrix(
                        LED_FLAG_KEYLIGHT |
                        LED_FLAG_MODIFIER |
                        LED_FLAG_INDICATOR
                    );
                    break;

                case (LED_FLAG_KEYLIGHT |
                      LED_FLAG_MODIFIER |
                      LED_FLAG_INDICATOR):
                    refresh_matrix(LED_FLAG_UNDERGLOW);
                    break;

                case LED_FLAG_UNDERGLOW:
                    enter_indicator_only_mode();
                    break;

                default:
                    leave_indicator_only_mode();
                    break;
            }
            return false;

        case KC_VOLD:
            if (record->event.pressed) {
                volume_indicator = VOL_INDICATOR_DOWN;
                volume_timer     = timer_read();
                mute_pressed     = false;
            }

            return true;

        case KC_VOLU:
            if (record->event.pressed) {
                volume_indicator = VOL_INDICATOR_UP;
                volume_timer     = timer_read();
                mute_pressed     = false;
            }

            return true;

        case KC_MUTE:
            if (record->event.pressed) {
                mute_pressed = !mute_pressed;
            }
            return true;
    }

    /*
     * Consume the release that belongs to a Sticky Hold or Turbo
     * selection/cancellation press.
     */
    if (!record->event.pressed &&
        keycode == suppressed_release_key) {
        suppressed_release_key = KC_NO;
        return false;
    }

    if (record->event.pressed && IS_QK_BASIC(keycode)) {
        /*
        * Pressing the held key again releases it.
        */
        if (sticky_hold_active && keycode == sticky_hold_key) {
            stop_sticky_hold();
            suppressed_release_key = keycode;
            return false;
        }

        /*
        * Capture the next basic key for Sticky Hold.
        */
        if (sticky_hold_armed) {
            sticky_hold_key        = keycode;
            sticky_hold_armed      = false;
            sticky_hold_active     = true;
            suppressed_release_key = keycode;

            register_code16(sticky_hold_key);
            return false;
        }

        /*
        * Pressing the Turbo target again stops Turbo.
        */
        if (turbo_active && keycode == turbo_key) {
            stop_turbo();
            suppressed_release_key = keycode;
            return false;
        }

        /*
        * Capture the next basic key for Turbo.
        */
        if (turbo_armed) {
            turbo_key              = keycode;
            turbo_timer            = timer_read();
            turbo_armed            = false;
            turbo_active           = true;
            suppressed_release_key = keycode;

            return false;
        }
    }

    /*
     * Sentence Case only treats period as a sentence ending because
     * shifted punctuation depends on the active operating-system layout.
     */
    if (sentence_case_enabled && record->event.pressed) {
        if (keycode == KC_DOT) {
            capitalize_next = true;
            return true;
        }

        if (capitalize_next &&
            keycode >= KC_A &&
            keycode <= KC_Z) {
            capitalize_next      = false;
            sentence_shifted_key = keycode;

            add_weak_mods(MOD_BIT(KC_LSFT));
            send_keyboard_report();

            return true;
        }
    }

    return true;
}

void matrix_scan_user(void) {
    if (mute_play_pressed &&
        !mute_play_triggered &&
        timer_elapsed(mute_play_timer) >= MUTE_PLAY_HOLD_MS) {
        tap_code16(KC_MPLY);
        mute_play_triggered = true;
    }

    if (volume_indicator != VOL_INDICATOR_NONE &&
        timer_elapsed(volume_timer) >= VOLUME_INDICATOR_TIMEOUT_MS) {
        volume_indicator = VOL_INDICATOR_NONE;
    }

    if (turbo_active &&
        turbo_key != KC_NO &&
        timer_elapsed(turbo_timer) >= TURBO_INTERVAL_MS) {
        tap_code16(turbo_key);
        turbo_timer = timer_read();
    }
}

bool rgb_matrix_indicators_user(void) {
    led_t host_leds = host_keyboard_led_state();

    uint8_t media_red   = 0;
    uint8_t media_green = 0;
    uint8_t media_blue  = 0;

    if (volume_indicator == VOL_INDICATOR_UP) {
        media_red = 255;
    } else if (volume_indicator == VOL_INDICATOR_DOWN) {
        media_blue = 255;
    } else if (mute_pressed) {
        uint8_t phase  = (timer_read() >> 5) & 0xFF;
        uint8_t breath = phase < 128
                            ? phase * 2
                            : (255 - phase) * 2;

        media_red   = breath;
        media_green = breath;
        media_blue  = breath;
    }

    set_media_leds(media_red, media_green, media_blue);

    /*
     * Blue is reserved for Caps Word or Sentence Case.
     * Scroll Lock is intentionally not included.
     */

    uint8_t status_blue = 0;

    #ifdef CAPS_WORD_ENABLE
        if (is_caps_word_on()) {
            status_blue = 255;
        }
    #endif

    if (sentence_case_enabled ||
        sticky_hold_active  ||
        sticky_hold_armed   ||
        turbo_active        ||
        turbo_armed) {
        status_blue = 255;
    }

    rgb_matrix_set_color(
        LED_STATUS,
        host_leds.caps_lock ? 255 : 0,
        keymap_config.no_gui ? 255 : 0,
        status_blue
    );

    rgb_matrix_set_color(LED_FN, 0, 0, 0);
    rgb_matrix_set_color(LED_CAPS, 0, 0, 0);

    rgb_matrix_set_color(
        LED_NUM,
        host_leds.num_lock ? 255 : 0,
        0,
        0
    );

    if (layer_state_cmp(layer_state, _FN)) {
        rgb_matrix_set_color(LED_FN, RGB_WHITE);
    }

    if (host_leds.caps_lock) {
        rgb_matrix_set_color(LED_CAPS, RGB_WHITE);
    }

    return false;
}

#ifdef ENCODER_MAP_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [_FN]   = {ENCODER_CCW_CW(RM_SATD, RM_SATU)},
};
#endif
