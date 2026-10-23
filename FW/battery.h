/**
 * @file battery.h
 * @brief Battery voltage check
 * @details Divider: battery - 1M - PA1 (AIN1) - 470k - PC3 (BV_GND).
 * PC3 is driven low only while measuring, otherwise high-Z so the divider
 * draws no current. 1.5 V internal reference, max. measurable ~4.7 V.
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <avr/io.h>
#include <stdint.h>

#define BATTERY_LOW_MV      3500  // warn when turning on
#define BATTERY_CRITICAL_MV 3300  // refuse to turn on / turn off; cleared above BATTERY_LOW_MV

#define BATTERY_R_TOP    1000  // kOhm, battery - PA1
#define BATTERY_R_BOTTOM 470   // kOhm, PA1 - PC3
#define BATTERY_VREF_MV  1500

// mV at the battery -> raw value of 16 accumulated 10-bit samples (no runtime division)
#define BATTERY_RAW(mv) ((uint32_t)(mv) * (16UL * 1023) / BATTERY_VREF_MV \
                         * BATTERY_R_BOTTOM / (BATTERY_R_TOP + BATTERY_R_BOTTOM))

typedef enum {
    BATTERY_OK,
    BATTERY_LOW,
    BATTERY_CRITICAL,
} battery_t;

static uint8_t battery_state = BATTERY_OK;

static void battery_init(void) {
    PORTA.PIN1CTRL = PORT_ISC_INPUT_DISABLE_gc;  // analog only, PA1 sits at battery voltage when idle
    PORTC.OUTCLR = PIN3_bm;                      // PC3 stays input (high-Z) until measuring

    VREF.CTRLA = VREF_ADC0REFSEL_1V5_gc;
    ADC0.CTRLB = ADC_SAMPNUM_ACC16_gc;
    ADC0.CTRLC = ADC_SAMPCAP_bm | ADC_REFSEL_INTREF_gc | ADC_PRESC_DIV16_gc;  // 1 MHz ADC clock
    ADC0.CTRLD = ADC_INITDLY_DLY64_gc;           // VREF startup
    ADC0.SAMPCTRL = 31;                          // long sampling, divider is ~320k source impedance
    ADC0.MUXPOS = ADC_MUXPOS_AIN1_gc;
}

/**
 * Measure and update battery_state
 */
static uint8_t battery_check(void) {
    PORTC.DIRSET = PIN3_bm;  // divider on
    ADC0.CTRLA = ADC_ENABLE_bm;
    delay_ms(1);

    ADC0.COMMAND = ADC_STCONV_bm;
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm));
    uint16_t raw = ADC0.RES;  // reading clears RESRDY

    ADC0.CTRLA = 0;
    PORTC.DIRCLR = PIN3_bm;  // divider off

    if (raw < BATTERY_RAW(BATTERY_CRITICAL_MV)) {
        battery_state = BATTERY_CRITICAL;
    } else if (raw < BATTERY_RAW(BATTERY_LOW_MV)) {
        if (battery_state != BATTERY_CRITICAL) battery_state = BATTERY_LOW;
    } else {
        battery_state = BATTERY_OK;
    }
    return battery_state;
}

#endif
