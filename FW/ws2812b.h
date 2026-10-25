/**
 * @file ws2812b.h
 * @brief WS2812b driver for ATtiny416, all LEDs share one color
 * @details Cycle-counted asm, 16 MHz (62.5 ns/cycle), 20 cycles per bit:
 * - 0-bit: ~6 cycles high (375 ns), ~14 low
 * - 1-bit: ~12 cycles high (750 ns), ~8 low
 */

#ifndef WS2812B_H
#define WS2812B_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#ifndef LED_COUNT
#define LED_COUNT 5  // override before including
#endif
#define LED_PIN   3  // PB3

/**
 * Height of the LED at strip position p (0 = bottom)
 * @details The strip is folded in half back to its start with the two halves
 * staggered, so going up the LEDs are 1, N, 2, N-1, 3, ... (for 5: 1 5 2 4 3).
 * Callers index LEDs by height, the driver sends them in strip order.
 */
static inline uint8_t ws2812b_height(uint8_t p) {
    return (p < (LED_COUNT + 1) / 2) ? 2 * p : 2 * (LED_COUNT - 1 - p) + 1;
}

static void ws2812b_init(void) {
    VPORTB.OUT &= ~(1 << LED_PIN);
    VPORTB.DIR |= (1 << LED_PIN);
}

static void ws2812b_send_byte(uint8_t b) {
    uint8_t n = 8;
    asm volatile(
        "1: sbi  %[port], %[pin]  \n"  // high
        "   rjmp .+0              \n"
        "   nop                   \n"
        "   sbrs %[b], 7          \n"
        "   cbi  %[port], %[pin]  \n"  // 0-bit: low here
        "   lsl  %[b]             \n"
        "   rjmp .+0              \n"
        "   rjmp .+0              \n"
        "   cbi  %[port], %[pin]  \n"  // 1-bit: low here
        "   rjmp .+0              \n"
        "   rjmp .+0              \n"
        "   nop                   \n"
        "   dec  %[n]             \n"
        "   brne 1b               \n"
        : [b] "+r" (b), [n] "+r" (n)
        : [port] "I" (_SFR_IO_ADDR(VPORTB_OUT)), [pin] "I" (LED_PIN)
    );
}

/**
 * Light the bottom n LEDs with one color, the rest off
 */
static void ws2812b_bar(uint8_t n, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t sreg = SREG;
    cli();
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        uint8_t on = ws2812b_height(i) < n;
        ws2812b_send_byte(on ? g : 0);  // GRB order
        ws2812b_send_byte(on ? r : 0);
        ws2812b_send_byte(on ? b : 0);
    }
    SREG = sreg;
    // Latch (>280 us low for WS2812B-2020 V6) is guaranteed by the caller's loop delay
}

#define ws2812b_set_all(r, g, b) ws2812b_bar(LED_COUNT, r, g, b)

/**
 * Send individual colors
 * @param rgb LED_COUNT * 3 bytes, R G B per LED, bottom LED first
 */
static inline void ws2812b_show(const uint8_t *rgb) {
    uint8_t sreg = SREG;
    cli();
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        const uint8_t *c = rgb + 3 * ws2812b_height(i);
        ws2812b_send_byte(c[1]);  // GRB order
        ws2812b_send_byte(c[0]);
        ws2812b_send_byte(c[2]);
    }
    SREG = sreg;
}

#endif
