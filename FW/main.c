/**
 * @file main.c
 * @author lcrm
 * @date 2026-09-10
 * @brief Main function
 * @details Hardware overview:
 * - Attiny416
 * - no external clock
 * - 4x WS2812b LED on pin PB3
 * - TMF8801 Distance Sensor:
 *   - SDA: PB1
 *   - SCL: PB0
 *   - INT: PC0
 *   - EN: PC1
 *   - GPIO0: PB4
 *   - GPIO1: PB2
 * - Battery divider 1M / 470k: measure PA1, low side PC3
 *
 * Requires fuse OSCCFG = 16 MHz (FREQSEL_16MHZ), factory default is 20 MHz.
 *
 * Power: while the light is off and nothing happens for IDLE_AFTER_MS (no hand,
 * no fade, no tone mode), the sensor measures every SENSOR_IDLE_MS and the MCU sleeps in
 * power-down, woken by the sensor INT pin (low on every result - the
 * TMF8801 has no distance threshold interrupt). A hand switches back to
 * SENSOR_ACTIVE_MS and the 10 ms main loop.
 *
 * Controls:
 * - wave: on / off
 * - hold + move: flame size / brightness (closer = smaller), remembered
 * - double wave: color mode (flash). Bring the hand back within
 *   TONE_MODE_WAIT_MS, the color follows it right away, from close to far:
 *   red, yellow, green, cyan, blue, magenta, red, warm white ... cool white.
 *   Ends TONE_MODE_END_MS after the hand left (flash), tone is remembered.
 *   No waves / brightness changes while in tone mode.
 */

// Doesn't fit 4 kB at -O0. -O2 is allowed by the free XC8 license (-Os is not),
// set here so it doesn't depend on the project settings.
#pragma GCC optimize("O2", "no-inline")  // inlining costs flash at -O2

#define F_CPU 16000000UL  // 16 MHz clock
#define DEBUG_DISTANCE 0  // 1 = show raw distance on LEDs, 0 = night light
#define BATTERY_CHECK  0  // 1 = battery divider fitted
#define LED_COUNT      4  // WS2812 LEDs on the strip
#define TICK_MS 10        // main loop period (fades, gesture timing)

#define ADJUST_NEAR_MM 60     // hold distance for value 0, +1 per mm up to 255 (~32 cm)
#define TONE_MODE_WAIT_MS 2000  // after the double wave, for the hand to come back
#define TONE_MODE_END_MS  1000  // after the hand left
#define BATTERY_PERIOD_MS 60000U

#define SENSOR_ACTIVE_MS 33   // measurement period while gestures are going on
#define SENSOR_IDLE_MS   100  // measurement period while idle (max 255), longer = quick waves get missed
#define IDLE_AFTER_MS    15000U // quiet this long with the light off -> idle

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include "ws2812b.h"
#include "twi.h"
#include "tmf8801.h"
#if !DEBUG_DISTANCE
#include "light.h"
#include "gesture.h"
#endif
#if BATTERY_CHECK
#include "battery.h"
#endif

/**
 * Busy-wait using TCB0 overflowing every 1 ms.
 * Used instead of util/delay.h, which pulls in float math at -O0.
 */
void delay_ms(uint8_t ms) {
    while (ms--) {
        while (!(TCB0.INTFLAGS & TCB_CAPT_bm));
        TCB0.INTFLAGS = TCB_CAPT_bm;
    }
}

#if DEBUG_DISTANCE
#define DBG 32  // debug brightness

// Color per 40 cm band: green, yellow, red, magenta, then white
static const uint8_t band_rgb[5][3] = {
    {0, DBG, 0}, {DBG, DBG, 0}, {DBG, 0, 0}, {DBG, 0, DBG}, {DBG, DBG, DBG},
};

/**
 * Debug display, 10 cm resolution:
 * - color = 40 cm band (green 0-40, yellow 40-80, red 80-120, magenta 120-160, white 160+)
 * - lit LEDs = 10 cm steps inside the band (1-4)
 * - first LED blue = sensor reports no target
 */
static void show_distance(uint16_t distance) {
    if (distance == TMF8801_NO_TARGET) {
        ws2812b_bar(1, 0, 0, DBG);
        return;
    }
    uint8_t cm10 = distance / 100;
    uint8_t band = cm10 / 4;
    if (band > 4) {
        ws2812b_set_all(DBG, DBG, DBG);
        return;
    }
    ws2812b_bar(cm10 % 4 + 1, band_rgb[band][0], band_rgb[band][1], band_rgb[band][2]);
}
#endif

/*
 * EEPROM layout (programming erases it unless the EESAVE fuse is set):
 * - page 0: sensor calibration, TMF8801_CAL_LEN bytes + magic
 * - page 1: settings (brightness, tone) + magic
 */
#define EEPROM_BYTES     ((uint8_t *)EEPROM_START)
#define CAL_ADDR         0
#define SETTINGS_ADDR    EEPROM_PAGE_SIZE
#define CAL_MAGIC        0xCA
#define SETTINGS_MAGIC   0x5E

/**
 * Write bytes within one EEPROM page
 */
static void eeprom_write(uint8_t addr, const uint8_t *data, uint8_t len) {
    while (NVMCTRL.STATUS & NVMCTRL_EEBUSY_bm);
    for (uint8_t i = 0; i < len; i++) EEPROM_BYTES[addr + i] = data[i];  // fills page buffer
    _PROTECTED_WRITE_SPM(NVMCTRL.CTRLA, NVMCTRL_CMD_PAGEERASEWRITE_gc);
    while (NVMCTRL.STATUS & NVMCTRL_EEBUSY_bm);
}

static void cal_save(const uint8_t *cal) {
    static const uint8_t magic = CAL_MAGIC;
    eeprom_write(CAL_ADDR, cal, TMF8801_CAL_LEN);
    eeprom_write(CAL_ADDR + TMF8801_CAL_LEN, &magic, 1);  // last, marks data valid
}

#if !DEBUG_DISTANCE
/**
 * Save brightness and tone if they changed
 */
static void settings_save(void) {
    uint8_t s[3] = {light_brightness, light_tone, SETTINGS_MAGIC};
    uint8_t *e = EEPROM_BYTES + SETTINGS_ADDR;
    if (e[0] != s[0] || e[1] != s[1] || e[2] != s[2]) eeprom_write(SETTINGS_ADDR, s, 3);
}

static void settings_load(void) {
    uint8_t *e = EEPROM_BYTES + SETTINGS_ADDR;
    if (e[2] == SETTINGS_MAGIC) {
        light_init(e[0], e[1]);
    } else {
        light_init(LIGHT_DEFAULT_BRIGHTNESS, LIGHT_DEFAULT_TONE);
    }
}

/**
 * Hold distance -> 0-255
 */
static uint8_t adjust_value(uint16_t mm) {
    if (mm <= ADJUST_NEAR_MM) return 0;
    if (mm >= ADJUST_NEAR_MM + 255) return 255;
    return mm - ADJUST_NEAR_MM;
}
#endif

/**
 * Blink red n times (blocking)
 */
static void blink_red(uint8_t n) {
    while (n--) {
        ws2812b_set_all(64, 0, 0);
        delay_ms(150);
        ws2812b_set_all(0, 0, 0);
        delay_ms(150);
    }
}

/**
 * Sensor error - blink red forever
 */
static void fail(void) {
    while (1) blink_red(1);
}

#if !DEBUG_DISTANCE
/**
 * Turn the light on/off, refuses / warns on low battery
 */
static void power(uint8_t on) {
#if BATTERY_CHECK
    if (on && !light_on) {
        uint8_t b = battery_check();
        if (b == BATTERY_CRITICAL) {
            blink_red(3);
            return;
        }
        if (b == BATTERY_LOW) blink_red(2);
    }
#endif
    light_set_on(on);
}
#endif

/**
 * Disable digital input on pins that are unconnected or float in sleep
 */
static void pins_init(void) {
    for (uint8_t i = 2; i < 8; i++) (&PORTA.PIN0CTRL)[i] = PORT_ISC_INPUT_DISABLE_gc;  // PA2-PA7 n.c.
    PORTB.PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc;  // sensor GPIO1
    PORTB.PIN4CTRL = PORT_ISC_INPUT_DISABLE_gc;  // sensor GPIO0
    PORTB.PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc;  // n.c.
    PORTC.PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc;  // n.c.
#if !BATTERY_CHECK
    PORTA.PIN1CTRL = PORT_ISC_INPUT_DISABLE_gc;  // divider not fitted
    PORTC.PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc;
#endif
    PORTC.PIN0CTRL = PORT_PULLUPEN_bm;           // sensor INT is open drain, no external pull-up
}

#if !DEBUG_DISTANCE
#define CAL_DATA (EEPROM_BYTES + CAL_ADDR)

static uint8_t tone_mode;
static uint16_t tone_timeout;
#if BATTERY_CHECK
static uint16_t battery_ticks;
#endif

static void sensor_period(uint8_t ms) {
    tmf8801_stop();
    if (tmf8801_start(CAL_DATA, ms)) fail();
}

/**
 * Sensor INT went low - wake up only, the result is read in the main loop
 */
ISR(PORTC_PORT_vect) {
    PORTC.PIN0CTRL = PORT_PULLUPEN_bm;  // level interrupt off until the next sleep
    PORTC.INTFLAGS = PIN0_bm;
}

/**
 * Power-down until the sensor INT pin is low (returns at once if it already is)
 */
static void sleep_until_sensor(void) {
    cli();
    PORTC.PIN0CTRL = PORT_PULLUPEN_bm | PORT_ISC_LEVEL_gc;  // PC0 isn't async, level works in power-down
    SLPCTRL.CTRLA = SLPCTRL_SMODE_PDOWN_gc | SLPCTRL_SEN_bm;
    sei();
    asm volatile("sleep");  // runs before a pending interrupt, so no wakeup is lost
    SLPCTRL.CTRLA = 0;
}

/**
 * @return 1 if nothing needs the 10 ms loop
 */
static uint8_t is_quiet(void) {
    return !light_on && gesture_idle() && !tone_mode && !light_fading();
}

/**
 * One TICK_MS step of gestures, modes, battery and light
 */
static void tick(void) {
    uint8_t g = gesture_update();
    switch (g) {
    case GESTURE_WAVE:
        power(!light_on);
        break;

    case GESTURE_DOUBLE_WAVE:
        power(1);
        if (!light_on) break;  // battery critical
        tone_mode = 1;
        tone_timeout = TONE_MODE_WAIT_MS / TICK_MS;
        gesture_set_tracking(1);
        light_flash();
        break;

    case GESTURE_HOLD:
    case GESTURE_HOLD_END:
        if (!light_on) break;
        light_set_brightness(adjust_value(gesture_mm));
        if (g == GESTURE_HOLD_END) settings_save();
        break;

    case GESTURE_TRACK:
    case GESTURE_TRACK_END:
        light_set_tone(adjust_value(gesture_mm));
        tone_timeout = TONE_MODE_END_MS / TICK_MS;
        break;
    }

    if (tone_mode && !--tone_timeout) {
        tone_mode = 0;
        gesture_set_tracking(0);
        settings_save();
        light_flash();
    }

#if BATTERY_CHECK
    if (++battery_ticks == BATTERY_PERIOD_MS / TICK_MS) {
        battery_ticks = 0;
        if (battery_check() == BATTERY_CRITICAL && light_on) {
            light_set_on(0);
            blink_red(3);
        }
    }
#endif

    light_update();
}
#endif

int main(void) {
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0);  // No prescaler -> full 16 MHz

    TCB0.CCMP = F_CPU / 1000 - 1;  // 1 ms period, polled by delay_ms
    TCB0.CTRLA = TCB_ENABLE_bm;

    // Watchdog, also runs in power-down: resets if the sensor ever stops waking us
    _PROTECTED_WRITE(WDT.CTRLA, WDT_PERIOD_8KCLK_gc);  // ~8 s

    pins_init();
    ws2812b_init();
    twi_init();
    ws2812b_set_all(0, 0, 0);
#if BATTERY_CHECK
    battery_init();
#endif

    if (tmf8801_init()) fail();

    // First boot after flashing (programming erases EEPROM): calibrate once
    if (EEPROM_BYTES[CAL_ADDR + TMF8801_CAL_LEN] != CAL_MAGIC) {
        uint8_t cal[TMF8801_CAL_LEN];
        if (tmf8801_calibrate(cal)) fail();
        cal_save(cal);
        ws2812b_set_all(32, 0, 32);  // magenta 1 s = calibration stored
        for (uint8_t i = 0; i < 4; i++) delay_ms(250);
        ws2812b_set_all(0, 0, 0);
    }

    uint16_t distance;
#if DEBUG_DISTANCE
    if (tmf8801_start(EEPROM_BYTES + CAL_ADDR, 33)) fail();

    uint8_t stale = 0;
    while (1) {
        __builtin_avr_wdr();
        if (tmf8801_get_distance(&distance) == 0) {
            show_distance(distance);
            stale = 0;
        } else if (++stale == 50) {
            // No new result for 500 ms - all cyan
            ws2812b_set_all(0, DBG, DBG);
        }
        delay_ms(TICK_MS);
    }
#else
    settings_load();
    if (tmf8801_start(CAL_DATA, SENSOR_ACTIVE_MS)) fail();

    uint16_t quiet_ticks = 0;
    while (1) {
        // Active: 10 ms loop
        __builtin_avr_wdr();
        if (tmf8801_get_distance(&distance) == 0) gesture_reading(distance);
        tick();
        delay_ms(TICK_MS);

        quiet_ticks = is_quiet() ? quiet_ticks + 1 : 0;
        if (quiet_ticks < IDLE_AFTER_MS / TICK_MS) continue;

        // Idle: sleep between slow measurements, catch up the ticks after each
        quiet_ticks = 0;
        sensor_period(SENSOR_IDLE_MS);
        do {
            __builtin_avr_wdr();
            sleep_until_sensor();
            if (tmf8801_get_distance(&distance)) {
                tmf8801_write(TMF8801_REG_INT_STATUS, TMF8801_INT_RESULT);  // don't spin on a stuck INT
                continue;
            }
            // Catch up the time since the last reading first: a hand seen now counts
            // as present from now, not for the whole period (that made waves look
            // like holds, which do nothing while the light is off)
            for (uint8_t i = 1; i < SENSOR_IDLE_MS / TICK_MS; i++) tick();
            gesture_reading(distance);
            tick();
        } while (is_quiet());
        sensor_period(SENSOR_ACTIVE_MS);
    }
#endif
}
