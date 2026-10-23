/**
 * @file tmf8801.h
 * @brief Minimal TMF8801 Time-of-Flight sensor driver
 * @details Boots the ROM measurement app (no RAM patch download - it is ~11 kB)
 * and polls results. Register map/command bytes follow the TMF8801 datasheet
 * DS000648 and the SparkFun / DFRobot TMF8801 libraries.
 */

#ifndef TMF8801_H
#define TMF8801_H

#include <avr/io.h>
#include <stdint.h>
#include "twi.h"

#define TMF8801_ADDR 0x41

#define TMF8801_REG_APPID     0x00
#define TMF8801_REG_APPREQID  0x02
#define TMF8801_REG_CMD_DATA7 0x08
#define TMF8801_REG_COMMAND   0x10
#define TMF8801_REG_CONTENTS  0x1E  // what is in 0x20.., see TMF8801_CONTENTS_*
#define TMF8801_REG_FACTORY_CAL 0x20
#define TMF8801_REG_ENABLE    0xE0
#define TMF8801_REG_INT_STATUS 0xE1
#define TMF8801_REG_INT_ENAB  0xE2
#define TMF8801_REG_ID        0xE3

#define TMF8801_INT_RESULT 0x01  // int1: every result, including "no target"

#define TMF8801_CHIP_ID   0x07
#define TMF8801_APP_MEAS  0xC0
#define TMF8801_CMD_MEAS  0x02
#define TMF8801_CMD_CAL   0x0A  // run factory calibration
#define TMF8801_CMD_LOAD_CAL 0x0B  // host is about to write calibration to 0x20
#define TMF8801_CMD_STOP  0xFF
#define TMF8801_CONTENTS_CAL    0x0A
#define TMF8801_CONTENTS_RESULT 0x55

#define TMF8801_CAL_LEN 14

#define TMF8801_NO_TARGET 0xFFFF

void delay_ms(uint8_t ms);

// CMD_DATA7..CMD_DATA0 for the measure command, "default mode" from datasheet DS000648 fig. 18
static const uint8_t tmf8801_meas_cfg[8] = {
    0x01,        // CMD_DATA7: factory calibration supplied in 0x20.., no algo state
    0x23,        // CMD_DATA6: bits 0,1 must be 1; bit 5 combined short+long histogram
                 //            (datasheet uses 0xA3, bit 7 = no standby: faster but more current)
    0x44,        // CMD_DATA5: GPIO0/1 unused, output low (startup default, nothing floats)
    0x00,        // CMD_DATA4: VCSEL pulse output timing (unused)
    0x00,        // CMD_DATA3: detection threshold 0, no spread spectrum
    33,          // CMD_DATA2: repetition period [ms], replaced by tmf8801_start()
    0x84, 0x03,  // CMD_DATA1/0: 900k iterations (0x0384) - more = longer range, slower
};

static uint8_t tmf8801_write(uint8_t reg, uint8_t val) {
    return twi_write_reg(TMF8801_ADDR, reg, &val, 1);
}

static uint8_t tmf8801_read(uint8_t reg) {
    uint8_t val = 0;
    twi_read_reg(TMF8801_ADDR, reg, &val, 1);
    return val;
}

/**
 * Poll a register until it equals the expected value
 * @param timeout roughly in ms
 */
static uint8_t tmf8801_wait_for(uint8_t reg, uint8_t val, uint16_t timeout) {
    while (timeout--) {
        if (tmf8801_read(reg) == val) return 0;
        delay_ms(1);
    }
    return 1;
}

/**
 * Power up the sensor and boot the measurement app
 * @return 0 on success
 */
static uint8_t tmf8801_init(void) {
    // EN pin (PC1) high
    PORTC.DIRSET = PIN1_bm;
    PORTC.OUTSET = PIN1_bm;
    delay_ms(5);

    // Power on, wait for CPU_READY (bit 6) + PON (bit 0)
    if (tmf8801_write(TMF8801_REG_ENABLE, 0x01)) return 1;
    if (tmf8801_wait_for(TMF8801_REG_ENABLE, 0x41, 100)) return 1;

    if ((tmf8801_read(TMF8801_REG_ID) & 0x3F) != TMF8801_CHIP_ID) return 1;

    // Switch from bootloader to the measurement app in ROM
    tmf8801_write(TMF8801_REG_APPREQID, TMF8801_APP_MEAS);
    return tmf8801_wait_for(TMF8801_REG_APPID, TMF8801_APP_MEAS, 100);
}

/**
 * Run factory calibration - no target within 40 cm, as dark as possible
 * @param cal receives TMF8801_CAL_LEN bytes to store on the host
 * @return 0 on success
 */
static uint8_t tmf8801_calibrate(uint8_t *cal) {
    tmf8801_write(TMF8801_REG_COMMAND, TMF8801_CMD_CAL);
    if (tmf8801_wait_for(TMF8801_REG_CONTENTS, TMF8801_CONTENTS_CAL, 5000)) return 1;
    if (twi_read_reg(TMF8801_ADDR, TMF8801_REG_FACTORY_CAL, cal, TMF8801_CAL_LEN)) return 1;
    tmf8801_write(TMF8801_REG_COMMAND, TMF8801_CMD_STOP);
    delay_ms(50);
    return 0;
}

/**
 * Load calibration and start continuous measurements
 * @param cal TMF8801_CAL_LEN bytes from tmf8801_calibrate()
 * @param period_ms time between measurements (one takes ~27 ms at 900k iterations)
 * @return 0 on success
 */
static uint8_t tmf8801_start(const uint8_t *cal, uint8_t period_ms) {
    uint8_t cfg[8];
    for (uint8_t i = 0; i < 8; i++) cfg[i] = tmf8801_meas_cfg[i];
    cfg[5] = period_ms;

    tmf8801_write(TMF8801_REG_INT_ENAB, TMF8801_INT_RESULT);  // INT pin low on each result
    tmf8801_write(TMF8801_REG_COMMAND, TMF8801_CMD_LOAD_CAL);
    if (twi_write_reg(TMF8801_ADDR, TMF8801_REG_FACTORY_CAL, cal, TMF8801_CAL_LEN)) return 1;
    if (twi_write_reg(TMF8801_ADDR, TMF8801_REG_CMD_DATA7, cfg, 8)) return 1;
    return tmf8801_write(TMF8801_REG_COMMAND, TMF8801_CMD_MEAS);
}

/**
 * Stop continuous measurement, a running one finishes first
 */
static inline void tmf8801_stop(void) {
    tmf8801_write(TMF8801_REG_COMMAND, TMF8801_CMD_STOP);
    delay_ms(50);
}

/**
 * Get distance if a new result is available
 * @param mm distance in mm, TMF8801_NO_TARGET if nothing detected
 * @return 0 if a new result was read
 */
static uint8_t tmf8801_get_distance(uint16_t *mm) {
    static uint8_t last_num;
    // CONTENTS, TID, RESULT_NUMBER, RESULT_INFO, DISTANCE_PEAK_0, DISTANCE_PEAK_1
    uint8_t r[6];

    if (twi_read_reg(TMF8801_ADDR, TMF8801_REG_CONTENTS, r, 6)) return 1;
    if (r[0] != TMF8801_CONTENTS_RESULT || r[2] == last_num) return 1;
    last_num = r[2];
    tmf8801_write(TMF8801_REG_INT_STATUS, TMF8801_INT_RESULT);  // release INT pin

    // RESULT_INFO bits 5:0 = reliability, 0 means no object
    *mm = (r[3] & 0x3F) ? (uint16_t)(r[4] | (r[5] << 8)) : TMF8801_NO_TARGET;
    return 0;
}

#endif
