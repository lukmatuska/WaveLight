/**
 * @file light.h
 * @brief Night light output: animated flame, on/off fades, gamma correction
 * @details Each LED flickers like a candle. The output level is kept as a
 * perceptual level (0-255, linear to the eye) and fades linearly in that
 * space, so after gamma correction the fade follows a curve that looks even.
 * Brightness sets the base heat of the flame (low = small red embers,
 * high = big bright flame). The tone picks the flame color, see
 * light_color(). light_update() must be called every TICK_MS, light_init()
 * once before.
 */

#ifndef LIGHT_H
#define LIGHT_H

#include <stdint.h>
#include "ws2812b.h"

#define LIGHT_FADE_ON_MS   800   // 0 -> full brightness
#define LIGHT_FADE_OFF_MS  1500  // full brightness -> 0
#define LIGHT_AUTO_OFF_MIN 0     // turn off after this many minutes, 0 = never

#define LIGHT_DEFAULT_BRIGHTNESS 255  // perceptual, LIGHT_MIN_BRIGHTNESS-255
#define LIGHT_MIN_BRIGHTNESS     24
#define LIGHT_DEFAULT_TONE       0    // see light_color(), 0 = red-orange flame

// LED white balance - WS2812 green and blue are much stronger than red.
// Tune until the 4000 K white (tone ~224) looks neutral. 255/255/255 = no correction.
#define LIGHT_WB_G 176
#define LIGHT_WB_B 240

#define FLAME_SPREAD    110  // flicker range above the base heat (base = brightness)
#define FLAME_CHANGE    40   // chance per tick (of 256) to pick a new flicker target
#define FLAME_GUST      2    // chance per tick (of 256) that the whole flame dips
#define FLAME_SPEED     2    // follow the target by 1/2^n per tick, higher = calmer
#define FLAME_HUE_SHIFT 24   // color shift at full heat (wheel: 43 = one sixth)

#define LIGHT_WHEEL_END 192  // tone 0..191 = color wheel, 192..255 = red -> white

// Fade steps per tick, level is 8.8 fixed point
#define LIGHT_STEP_ON  (0xFF00U / (LIGHT_FADE_ON_MS / TICK_MS))
#define LIGHT_STEP_OFF (0xFF00U / (LIGHT_FADE_OFF_MS / TICK_MS))

// White strip points, interpolated: red, then black body colors converted
// from sRGB to linear LED values ((v/255)^2.2)
static const uint8_t white_rgb[5][3] = {
    {255,   0,   0},  // red (end of the wheel)
    {255, 103,  24},  // 2700 K
    {255, 165,  95},  // 4000 K
    {255, 199, 159},  // 5000 K
    {255, 242, 251},  // 6500 K
};

static uint8_t light_on;
static uint8_t light_brightness;
static uint8_t light_tone;
static uint16_t light_level;  // current perceptual level, 8.8 fixed point

static uint8_t flame_heat[LED_COUNT];
static uint8_t flame_target[LED_COUNT];
static uint16_t flame_rng = 0xACE1;

/**
 * Gamma 2 correction, any nonzero level gives at least 1
 */
static uint8_t gamma8(uint8_t l) {
    return ((uint16_t)l * l + 255) >> 8;
}

/**
 * a * b / 255 - small channels may drop to 0, so dim light gets warmer
 */
static uint8_t scale8(uint8_t a, uint8_t b) {
    return ((uint16_t)a * b + a) >> 8;
}

#if LIGHT_AUTO_OFF_MIN
#define LIGHT_TICKS_PER_MIN (60000U / TICK_MS)
static uint16_t light_ticks;
static uint8_t light_minutes;
#endif

static void light_set_on(uint8_t on) {
    light_on = on;
#if LIGHT_AUTO_OFF_MIN
    light_ticks = 0;
    light_minutes = 0;
#endif
}

static void light_set_brightness(uint8_t brightness) {
    light_brightness = brightness < LIGHT_MIN_BRIGHTNESS ? LIGHT_MIN_BRIGHTNESS : brightness;
}

/**
 * @param tone see light_color()
 */
static void light_set_tone(uint8_t tone) {
    light_tone = tone;
}

static void light_init(uint8_t brightness, uint8_t tone) {
    light_set_brightness(brightness);
    light_set_tone(tone);
}

/**
 * xorshift16 pseudo random
 */
static uint8_t flame_rand(void) {
    flame_rng ^= flame_rng << 7;
    flame_rng ^= flame_rng >> 9;
    flame_rng ^= flame_rng << 8;
    return flame_rng;
}

/**
 * Color for a tone, gamma corrected so the steps look even, white balanced
 * - 0..191: color wheel red, yellow (32), green (64), cyan (96), blue (128),
 *   magenta (160), back to red
 * - 192..255: red, warm white (2700 K, 208) ... cool white (6500 K, 255)
 * @param shift moves along the wheel / towards cooler white (flame heat)
 */
static void light_color(uint8_t tone, uint8_t shift, uint8_t *rgb) {
    // Per sixth of the wheel: channel at full, channel ramping (up on even sixths)
    static const uint8_t wheel_full[6] = {0, 1, 1, 2, 2, 0};
    static const uint8_t wheel_ramp[6] = {1, 0, 2, 1, 0, 2};

    rgb[0] = rgb[1] = rgb[2] = 0;
    if (tone < LIGHT_WHEEL_END) {
        uint8_t hue = tone + tone / 3 + shift;  // 0..191 -> 0..254, wraps around the wheel
        uint8_t seg = hue / 43;
        uint8_t f = (hue - seg * 43) * 6;

        rgb[wheel_full[seg]] = 255;
        rgb[wheel_ramp[seg]] = gamma8(seg & 1 ? 255 - f : f);
    } else {
        uint16_t w = (tone - LIGHT_WHEEL_END) * 4 + shift;  // 0..255, no wrap
        if (w > 255) w = 255;
        const uint8_t *p = white_rgb[w >> 6];  // interpolate p -> p + 3 (next point)
        uint8_t frac = (w & 63) << 2;

        for (uint8_t c = 0; c < 3; c++) rgb[c] = p[c] + (((int16_t)p[c + 3] - p[c]) * frac >> 8);
    }

    rgb[1] = scale8(rgb[1], LIGHT_WB_G);
    rgb[2] = scale8(rgb[2], LIGHT_WB_B);
}

/**
 * Advance the flicker by one tick and render it at the given output level
 * @param rgb receives LED_COUNT * 3 bytes
 */
static void flame_render(uint8_t level, uint8_t *rgb) {
    uint8_t gust = flame_rand() < FLAME_GUST;
    uint8_t base = scale8(light_brightness, 255 - FLAME_SPREAD);

    for (uint8_t i = 0; i < LED_COUNT; i++, rgb += 3) {
        if (gust) {
            flame_target[i] = base;
        } else if (flame_rand() < FLAME_CHANGE) {
            flame_target[i] = base + scale8(flame_rand(), FLAME_SPREAD);
        }

        int16_t h = flame_heat[i];
        h += (flame_target[i] - h) >> FLAME_SPEED;
        if (i) h = (3 * h + flame_heat[i - 1]) >> 2;  // couple neighbors, flicker travels along
        flame_heat[i] = h;

        // Hotter = brighter (heat is perceptual, like brightness) and a bit
        // further along the color strip
        uint8_t v = scale8(level, gamma8(h));
        light_color(light_tone, scale8(h, FLAME_HUE_SHIFT), rgb);
        for (uint8_t c = 0; c < 3; c++) rgb[c] = scale8(rgb[c], v);
    }
}

static uint16_t light_target(void) {
    return light_on ? 0xFF00 : 0;  // brightness goes into the flame heat
}

/**
 * @return 1 while fading (light_update() must keep running)
 */
static uint8_t light_fading(void) {
    return light_level != light_target();
}

/**
 * Dip to dark and fade back - acknowledges a mode change
 */
static void light_flash(void) {
    light_level = 0;
}

/**
 * Advance the fade and the flame by one tick and refresh the LEDs
 */
static void light_update(void) {
#if LIGHT_AUTO_OFF_MIN
    if (light_on && ++light_ticks == LIGHT_TICKS_PER_MIN) {
        light_ticks = 0;
        if (++light_minutes == LIGHT_AUTO_OFF_MIN) light_on = 0;
    }
#endif
    uint16_t target = light_target();

    if (light_level < target) {
        light_level = (target - light_level > LIGHT_STEP_ON) ? light_level + LIGHT_STEP_ON : target;
    } else if (light_level > target) {
        light_level = (light_level - target > LIGHT_STEP_OFF) ? light_level - LIGHT_STEP_OFF : target;
    }

    uint8_t rgb[LED_COUNT * 3];
    flame_render(gamma8(light_level >> 8), rgb);
    ws2812b_show(rgb);
}

#endif
