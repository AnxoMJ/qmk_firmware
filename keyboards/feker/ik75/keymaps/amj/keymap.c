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
 * How long the volume-up or volume-down indicator remains visible.
 * The value is in milliseconds.
 */
#define VOLUME_INDICATOR_TIMEOUT_MS 100

/*
 * RGB Matrix LED indexes.
 *
 * These indexes correspond to entries in the rgb_matrix.layout array
 * in keyboards/feker/ik75/keyboard.json.
 */
#define LED_FN      31
#define LED_CAPS    32
#define LED_NUM     46

#define LED_MEDIA1  99
#define LED_MEDIA2 100
#define LED_MEDIA3 101

#define LED_STATUS 104

/*
 * Layers.
 */
enum layer_names {
    _BASE,
    _FN,
};

/*
 * Only one temporary volume indicator can be active at a time.
 */
typedef enum {
    VOL_INDICATOR_NONE,
    VOL_INDICATOR_UP,
    VOL_INDICATOR_DOWN,
} volume_indicator_t;

/*
 * Local indicator state.
 *
 * The operating system does not normally report its audio mute state
 * back to QMK. Therefore, mute_pressed tracks mute operations generated
 * by this keyboard, not necessarily the current Linux audio state.
 */
static bool               mute_pressed   = false;
static volume_indicator_t volume_indicator = VOL_INDICATOR_NONE;
static uint16_t           volume_timer   = 0;

/*
 * Keymaps.
 *
 * The top-right physical key is KC_MUTE on the base layer and RGB_TOG
 * while the Fn layer is active.
 */
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BASE] = LAYOUT(
        KC_ESC,  KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  KC_DEL,           KC_MUTE,
        KC_GRV,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,          KC_INS,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS,          KC_END,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,           KC_PGUP,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH,          KC_RSFT, KC_UP,   KC_PGDN,
        KC_LCTL, KC_LGUI, KC_LALT,                            KC_SPC,                     KC_RALT, MO(_FN),           KC_RCTL, KC_LEFT, KC_DOWN, KC_RIGHT
    ),

    [_FN] = LAYOUT(
        _______, KC_MYCM, KC_WHOM, KC_CALC, KC_SLCT, KC_MPRV, KC_MNXT, KC_MPLY, KC_MSTP, KC_MUTE, KC_VOLD, KC_VOLU, KC_MAIL, _______,          RGB_TOG,
        NK_TOGG, KC_P1,   KC_P2,   KC_P3,   KC_P4,   KC_P5,   KC_P6,   KC_P7,   KC_P8,   KC_P9,   KC_P0,   RGB_SPD, RGB_SPI, _______,          RGB_MOD,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_PSCR, _______, _______, _______,          RGB_RMOD,
        _______, _______, KC_SCRL, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______,          RGB_HUI,
        _______,          _______, _______, _______, _______, _______, KC_NUM,  _______, _______, _______, _______,          _______, RGB_VAI, RGB_HUD,
        _______, GUI_TOG, _______,                            _______,                   _______, _______,          _______, RGB_SPD, RGB_VAD, RGB_SPI
    ),
};

/*
 * Set all three media-indicator LEDs to the same color.
 */
static void set_media_leds(uint8_t red, uint8_t green, uint8_t blue) {
    rgb_matrix_set_color(LED_MEDIA1, red, green, blue);
    rgb_matrix_set_color(LED_MEDIA2, red, green, blue);
    rgb_matrix_set_color(LED_MEDIA3, red, green, blue);
}

/*
 * Change which groups of RGB LEDs are controlled by animations.
 *
 * RGB Matrix remains enabled even when LED_FLAG_NONE is selected.
 * This allows rgb_matrix_indicators_user() to continue showing status
 * indicators while decorative RGB lighting is off.
 */
static void refresh_matrix(uint8_t led_flags) {
    rgb_matrix_set_flags(led_flags);
    rgb_matrix_set_color_all(0, 0, 0);
}

/*
 * Process custom key behavior.
 */
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        /*
         * Cycle through:
         *
         * 1. All LEDs
         * 2. Key, modifier and indicator LEDs
         * 3. Underglow LEDs
         * 4. Decorative RGB off
         *
         * Status indicators continue to be drawn in every mode.
         */
        case RGB_TOG:
            if (!record->event.pressed) {
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
                    refresh_matrix(LED_FLAG_NONE);
                    break;

                default:
                    refresh_matrix(LED_FLAG_ALL);
                    rgb_matrix_enable_noeeprom();
                    break;
            }

            /*
             * Prevent QMK from also applying the standard RGB_TOG action.
             */
            return false;

        /*
         * Volume down shows a temporary blue indicator.
         *
         * Changing the volume also clears the locally tracked mute state,
         * matching the desired indicator behavior.
         */
        case KC_VOLD:
            if (record->event.pressed) {
                volume_indicator = VOL_INDICATOR_DOWN;
                volume_timer     = timer_read();
                mute_pressed     = false;
            }
            return true;

        /*
         * Volume up shows a temporary red indicator.
         */
        case KC_VOLU:
            if (record->event.pressed) {
                volume_indicator = VOL_INDICATOR_UP;
                volume_timer     = timer_read();
                mute_pressed     = false;
            }
            return true;

        /*
         * Toggle the locally tracked mute state.
         *
         * Returning true allows QMK to send KC_MUTE to Linux normally.
         */
        case KC_MUTE:
            if (record->event.pressed) {
                mute_pressed = !mute_pressed;
            }
            return true;
    }

    return true;
}

/*
 * Remove the temporary volume indicator after its timeout expires.
 *
 * timer_read() and timer_elapsed() handle the 16-bit timer wrapping
 * automatically.
 */
void matrix_scan_user(void) {
    if (volume_indicator != VOL_INDICATOR_NONE &&
        timer_elapsed(volume_timer) >= VOLUME_INDICATOR_TIMEOUT_MS) {
        volume_indicator = VOL_INDICATOR_NONE;
    }
}

/*
 * Draw keyboard state indicators after the selected RGB animation.
 *
 * These indicators are drawn regardless of the current RGB flags,
 * including LED_FLAG_NONE.
 */
bool rgb_matrix_indicators_user(void) {
    /*
     * Read the host LED state once so every indicator uses the same
     * state snapshot during this RGB frame.
     */
    led_t host_leds = host_keyboard_led_state();

    /*
     * Determine the media-indicator color before writing to the LEDs.
     * This ensures the three LEDs are written only once per frame.
     *
     * Priority:
     *   1. Volume up flash
     *   2. Volume down flash
     *   3. Persistent mute state
     *   4. Off
     */
    uint8_t media_red   = 0;
    uint8_t media_green = 0;
    uint8_t media_blue  = 0;

    if (volume_indicator == VOL_INDICATOR_UP) {
        media_red = 255;
    } else if (volume_indicator == VOL_INDICATOR_DOWN) {
        media_blue = 255;
    } else if (mute_pressed) {
        media_red   = 255;
        media_green = 255;
        media_blue  = 255;
    }

    set_media_leds(media_red, media_green, media_blue);

    /*
     * LED_STATUS combines three states into one RGB LED:
     *
     * Red:   Caps Lock
     * Green: GUI key disabled
     * Blue:  Scroll Lock
     *
     * Multiple active states will combine their colors.
     */
    rgb_matrix_set_color(
        LED_STATUS,
        host_leds.caps_lock ? 255 : 0,
        keymap_config.no_gui ? 255 : 0,
        host_leds.scroll_lock ? 255 : 0
    );

    /*
     * Num Lock indicator.
     */
    rgb_matrix_set_color(
        LED_NUM,
        host_leds.num_lock ? 255 : 0,
        0,
        0
    );

    /*
     * Fn-layer indicator.
     */
    if (layer_state_is(_FN)) {
        rgb_matrix_set_color(LED_FN, RGB_WHITE);
    }

    /*
     * Dedicated Caps Lock indicator.
     */
    if (host_leds.caps_lock) {
        rgb_matrix_set_color(LED_CAPS, RGB_WHITE);
    }

    return false;
}

/*
 * Encoder behavior:
 *
 * Base layer: volume down/up
 * Fn layer:   RGB saturation down/up
 */
#ifdef ENCODER_MAP_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [_FN]   = {ENCODER_CCW_CW(RGB_SAD, RGB_SAI)},
};
#endif
