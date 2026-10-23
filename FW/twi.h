/**
 * @file twi.h
 * @brief Minimal TWI (I2C) master driver for ATtiny416
 * @details Polled, no smart mode. SDA = PB1, SCL = PB0 (default pins).
 */

#ifndef TWI_H
#define TWI_H

#include <avr/io.h>
#include <stdint.h>

// Slow clock: no external pull-ups, the weak internal ones (~20-50k) need long rise time
#define TWI_FREQ 50000UL
#define TWI_BAUD ((F_CPU / (2 * TWI_FREQ)) - 5)

static void twi_init(void) {
    PORTB.PIN0CTRL = PORT_PULLUPEN_bm;  // SCL
    PORTB.PIN1CTRL = PORT_PULLUPEN_bm;  // SDA
    TWI0.MBAUD = TWI_BAUD;
    TWI0.MCTRLA = TWI_ENABLE_bm;
    TWI0.MSTATUS = TWI_BUSSTATE_IDLE_gc;  // Force bus state to idle
}

/**
 * Wait for the current byte/address to finish
 * @return 0 on ACK, nonzero on NACK / arbitration lost / bus error
 */
static uint8_t twi_wait(void) {
    uint8_t s;
    while (!((s = TWI0.MSTATUS) & (TWI_WIF_bm | TWI_RIF_bm)));
    return s & (TWI_RXACK_bm | TWI_ARBLOST_bm | TWI_BUSERR_bm);
}

/**
 * Send (repeated) START + address, then register pointer
 */
static uint8_t twi_start_reg(uint8_t addr, uint8_t reg) {
    TWI0.MADDR = addr << 1;
    if (twi_wait()) return 1;
    TWI0.MDATA = reg;
    return twi_wait();
}

static uint8_t twi_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t len) {
    uint8_t err = twi_start_reg(addr, reg);

    while (!err && len--) {
        TWI0.MDATA = *data++;
        err = twi_wait();
    }

    TWI0.MCTRLB = TWI_MCMD_STOP_gc;
    return err;
}

static uint8_t twi_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len) {
    if (twi_start_reg(addr, reg)) {
        TWI0.MCTRLB = TWI_MCMD_STOP_gc;
        return 1;
    }

    // Repeated START with read bit, first byte is clocked in automatically
    TWI0.MADDR = (addr << 1) | 1;
    if (twi_wait()) {
        TWI0.MCTRLB = TWI_MCMD_STOP_gc;
        return 1;
    }

    while (1) {
        *data++ = TWI0.MDATA;
        if (!--len) break;
        TWI0.MCTRLB = TWI_MCMD_RECVTRANS_gc;  // ACK and read next byte
        twi_wait();
    }

    TWI0.MCTRLB = TWI_ACKACT_NACK_gc | TWI_MCMD_STOP_gc;  // NACK last byte + STOP
    return 0;
}

#endif
