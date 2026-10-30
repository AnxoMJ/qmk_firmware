/* Copyright 2022 Feker
 * Copyright 2022 HorrorTroll <https://github.com/HorrorTroll>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H

/*
 * Temporary volume indicator duration.
 */
#define VOLUME_INDICATOR_TIMEOUT_MS 25

/*
 * Tap before 200 ms:
 *   Mute/Unmute.
 *
 * Hold for 200 ms:
 *   Play/Pause immediately, without waiting for release.
 */
#define MUTE_PLAY_HOLD_MS 200

/*
 * Tap before 150 ms:
 *   Caps Lock.
 *
 * Hold for at least 150 ms and release:
 *   Enable Caps Word.
 */
#define CAPS_WORD_HOLD_MS 150

/*
 * RGB Matrix LED indexes from keyboard.json.
 */
#define LED_FN      31
#define LED_CAPS    32
#define LED_NUM     46
#define LED_MEDIA1  99
#define LED_MEDIA2 100
#define LED_MEDIA3 101
#define LED_STATUS 104

/*
 * Custom Sentence Case state.
 *
 * Fn + Q toggles this feature.
 */
static bool sentence_case_enabled = false;
static bool capitalize_next       = false;

/*
 * Keyboard layers.
 */
enum layer_names {
    _BASE,
    _FN,
};

/*
 * Custom keycodes.
 */
enum custom_keycodes {
    MUTE_PLAY = SAFE_RANGE,
    CW_CAPS,
    SENT_CASE,
};

/*
 * Caps Lock/Caps Word timer.
 */
static uint16_t caps_timer = 0;

/*
 * Temporary volume indicator state.
 */
typedef enum {
    VOL_INDICATOR_NONE,
    VOL_INDICATOR_UP,
    VOL_INDICATOR_DOWN,
} volume_indicator_t;

/*
 * Mute and volume indicator state.
 *
 * The mute state is tracked locally because Linux does not normally
 * report its audio mute state back to QMK.
 */
static bool               mute_pressed     = false;
static volume_indicator_t volume_indicator = VOL_INDICATOR_NONE;
static uint16_t           volume_timer     = 0;

/*
 * Multifunction Mute/Play key state.
 */
static uint16_t mute_play_timer     = 0;
static bool     mute_play_pressed   = false;
static bool     mute_play_triggered = false;

/*
 * Indicator-only RGB mode state.
 *
 * When decorative RGB is turned off, the previous RGB effect is saved.
 * RGB Matrix is switched to SOLID_COLOR with LED_FLAG_NONE. This keeps
 * the indicator callback running without calculating a heavy effect.
 */
static bool    indicator_only_mode = false;
static uint8_t saved_rgb_mode      = RGB_MATRIX_SOLID_COLOR;

/*
 * Keymaps.
 *
 * Top-right physical key:
 *
 * Base:
 *   Tap             = Mute/Unmute
 *   Hold for 200 ms = Play/Pause
 *
 * Fn:
 *   Cycle RGB Matrix LED groups
 *
 * Other custom functions:
 *   Tap Caps Lock  = Caps Lock
 *   Hold Caps Lock = Caps Word after release
 *   Fn + Q         = Toggle Sentence Case
 */
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
        _______, SENT_CASE, _______, _______, _______, _______, _______, _______, _______, _______, KC_PSCR, _______, _______, _______,          RM_PREV,
        _______, _______, KC_SCRL, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______,          RM_HUEU,
        _______,          _______, _______, _______, _______, _______, KC_NUM,  _______, _______, _______, _______,          _______, RM_VALU, RM_HUED,
        _______, GU_TOGG, _______,                            _______,                   _______, _______,          _______, RM_SPDD, RM_VALD, RM_SPDU
    ),
};

/*
 * Set all media LEDs to one color.
 */
static void set_media_leds(uint8_t red, uint8_t green, uint8_t blue) {
    rgb_matrix_set_color(LED_MEDIA1, red, green, blue);
    rgb_matrix_set_color(LED_MEDIA2, red, green, blue);
    rgb_matrix_set_color(LED_MEDIA3, red, green, blue);
}

/*
 * Select which LED groups receive the active RGB Matrix effect.
 */
static void refresh_matrix(uint8_t led_flags) {
    rgb_matrix_set_flags(led_flags);
    rgb_matrix_set_color_all(0, 0, 0);
}

/*
 * Enter the low-processing indicator-only mode.
 *
 * RGB Matrix remains enabled because disabling it completely would also
 * stop rgb_matrix_indicators_user() from being called.
 */
static void enter_indicator_only_mode(void) {
    if (!indicator_only_mode) {
        saved_rgb_mode = rgb_matrix_get_mode();
    }

    indicator_only_mode = true;

    /*
     * SOLID_COLOR requires less processing than effects such as
     * Digital Rain, Typing Heatmap, Riverflow, and reactive effects.
     */
    rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
    refresh_matrix(LED_FLAG_NONE);
}

/*
 * Leave indicator-only mode and restore the previous RGB effect.
 */
static void leave_indicator_only_mode(void) {
    indicator_only_mode = false;

    rgb_matrix_enable_noeeprom();
    rgb_matrix_mode_noeeprom(saved_rgb_mode);
    refresh_matrix(LED_FLAG_ALL);
}

/*
 * Process custom key behavior.
 */
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        /*
         * Toggle the custom Sentence Case mode.
         *
         * The first letter after enabling the feature is capitalized.
         * Press Fn + Q again to disable it.
         */
        case SENT_CASE:
            if (record->event.pressed) {
                sentence_case_enabled = !sentence_case_enabled;
                capitalize_next       = sentence_case_enabled;
            }

            return false;

        /*
         * Tap:
         *   Mute/Unmute.
         *
         * Hold:
         *   Play/Pause after MUTE_PLAY_HOLD_MS.
         */
        case MUTE_PLAY:
            if (record->event.pressed) {
                mute_play_pressed   = true;
                mute_play_triggered = false;
                mute_play_timer     = timer_read();
            } else {
                mute_play_pressed = false;

                /*
                 * If Play/Pause did not fire, this was a short press.
                 */
                if (!mute_play_triggered) {
                    tap_code16(KC_MUTE);
                    mute_pressed = !mute_pressed;
                }
            }

            return false;

        /*
         * Tap:
         *   Caps Lock.
         *
         * Hold and release:
         *   Enable Caps Word.
         *
         * The action is decided only when the key is released.
         */
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

        /*
         * RGB group cycle:
         *
         * 1. All LEDs
         * 2. Key, modifier and indicator LEDs
         * 3. Underglow
         * 4. Indicators only
         */
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

        /*
         * Volume-down indicator.
         */
        case KC_VOLD:
            if (record->event.pressed) {
                volume_indicator = VOL_INDICATOR_DOWN;
                volume_timer     = timer_read();
                mute_pressed     = false;
            }

            return true;

        /*
         * Volume-up indicator.
         */
        case KC_VOLU:
            if (record->event.pressed) {
                volume_indicator = VOL_INDICATOR_UP;
                volume_timer     = timer_read();
                mute_pressed     = false;
            }

            return true;

        /*
         * Track mute state generated by this keyboard.
         */
        case KC_MUTE:
            if (record->event.pressed) {
                mute_pressed = !mute_pressed;
            }

            return true;
    }

    /*
     * Custom Sentence Case mode.
     *
     * This lightweight implementation automatically capitalizes:
     *   - the first letter after enabling the mode
     *   - the first letter after a period
     *
     * Digits, spaces, and other non-letter keys do not clear the pending
     * capitalization. This allows ". 123 example" to become
     * ". 123 Example".
     */
    if (sentence_case_enabled && record->event.pressed) {
        /*
         * Period marks the end of a sentence.
         *
         * Only KC_DOT is used here because shifted punctuation depends
         * on the active Linux keyboard layout.
         */
        if (keycode == KC_DOT) {
            capitalize_next = true;
            return true;
        }

        /*
         * Capitalize the next alphabetic key.
         */
        if (capitalize_next &&
            keycode >= KC_A &&
            keycode <= KC_Z) {
            capitalize_next = false;

            /*
             * Preserve all modifier state while the uppercase letter is
             * generated.
             */
            uint8_t saved_mods         = get_mods();
            uint8_t saved_weak_mods    = get_weak_mods();
            uint8_t saved_oneshot_mods = get_oneshot_mods();

            clear_mods();
            clear_weak_mods();
            clear_oneshot_mods();

            register_code(KC_LSFT);
            tap_code(keycode);
            unregister_code(KC_LSFT);

            set_mods(saved_mods);
            set_weak_mods(saved_weak_mods);
            set_oneshot_mods(saved_oneshot_mods);
            send_keyboard_report();

            return false;
        }
    }

    return true;
}

/*
 * Handle time-dependent actions.
 */
void matrix_scan_user(void) {
    /*
     * Fire Play/Pause as soon as the hold threshold is reached.
     */
    if (mute_play_pressed &&
        !mute_play_triggered &&
        timer_elapsed(mute_play_timer) >= MUTE_PLAY_HOLD_MS) {
        tap_code16(KC_MPLY);
        mute_play_triggered = true;
    }

    /*
     * Remove the temporary volume indicator.
     */
    if (volume_indicator != VOL_INDICATOR_NONE &&
        timer_elapsed(volume_timer) >= VOLUME_INDICATOR_TIMEOUT_MS) {
        volume_indicator = VOL_INDICATOR_NONE;
    }
}

/*
 * Draw keyboard indicators after the current RGB Matrix frame.
 *
 * This callback remains active in indicator-only mode because RGB
 * Matrix itself stays enabled.
 */
bool rgb_matrix_indicators_user(void) {
    led_t host_leds = host_keyboard_led_state();

    /*
     * Determine the final media LED color.
     *
     * Priority:
     *   1. Volume up:   red
     *   2. Volume down: blue
     *   3. Muted:       breathing white
     *   4. Otherwise:   off
     */
    uint8_t media_red   = 0;
    uint8_t media_green = 0;
    uint8_t media_blue  = 0;

    if (volume_indicator == VOL_INDICATOR_UP) {
        media_red = 255;
    } else if (volume_indicator == VOL_INDICATOR_DOWN) {
        media_blue = 255;
    } else if (mute_pressed) {
        /*
         * Breathing white indicator.
         *
         * Brightness follows a lightweight triangle wave:
         *   0 -> 254 -> 0
         *
         * With timer_read() shifted by five bits, a complete cycle
         * takes approximately 8.2 seconds.
         */
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
     * Combined status LED:
     *
     * Red:   Caps Lock
     * Green: GUI disabled
     * Blue:  Scroll Lock, Caps Word, or Sentence Case
     *
     * Multiple active states combine their colors.
     */
    uint8_t status_blue = host_leds.scroll_lock ? 255 : 0;

#ifdef CAPS_WORD_ENABLE
    if (is_caps_word_on()) {
        status_blue = 255;
    }
#endif

    if (sentence_case_enabled) {
        status_blue = 255;
    }

    rgb_matrix_set_color(
        LED_STATUS,
        host_leds.caps_lock ? 255 : 0,
        keymap_config.no_gui ? 255 : 0,
        status_blue
    );

    /*
     * Clear conditional LEDs before redrawing them.
     *
     * This prevents colors from getting stuck in underglow-only and
     * indicator-only modes.
     */
    rgb_matrix_set_color(LED_FN, 0, 0, 0);
    rgb_matrix_set_color(LED_CAPS, 0, 0, 0);

    /*
     * Num Lock indicator: red while Num Lock is enabled.
     */
    rgb_matrix_set_color(
        LED_NUM,
        host_leds.num_lock ? 255 : 0,
        0,
        0
    );

    /*
     * Fn-layer indicator: white while the Fn layer is active.
     */
    if (layer_state_cmp(layer_state, _FN)) {
        rgb_matrix_set_color(LED_FN, RGB_WHITE);
    }

    /*
     * Caps Lock indicator: white while Caps Lock is active.
     */
    if (host_leds.caps_lock) {
        rgb_matrix_set_color(LED_CAPS, RGB_WHITE);
    }

    return false;
}

/*
 * Encoder behavior:
 *
 * Base: volume down/up
 * Fn:   RGB Matrix saturation down/up
 */
#ifdef ENCODER_MAP_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [_FN]   = {ENCODER_CCW_CW(RM_SATD, RM_SATU)},
};
#endif
