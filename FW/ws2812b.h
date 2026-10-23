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
#define LED_COUNT 4  // override before including
#endif
#define LED_PIN   3  // PB3

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
 * Light the first n LEDs with one color, the rest off
 */
static void ws2812b_bar(uint8_t n, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t sreg = SREG;
    cli();
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        if (i == n) r = g = b = 0;
        ws2812b_send_byte(g);  // GRB order
        ws2812b_send_byte(r);
        ws2812b_send_byte(b);
    }
    SREG = sreg;
    // Latch (>50 us low) is guaranteed by the caller's loop delay
}

#define ws2812b_set_all(r, g, b) ws2812b_bar(LED_COUNT, r, g, b)

/**
 * Send individual colors
 * @param rgb LED_COUNT * 3 bytes, R G B per LED
 */
static inline void ws2812b_show(const uint8_t *rgb) {
    uint8_t sreg = SREG;
    cli();
    for (uint8_t i = 0; i < LED_COUNT; i++, rgb += 3) {
        ws2812b_send_byte(rgb[1]);  // GRB order
        ws2812b_send_byte(rgb[0]);
        ws2812b_send_byte(rgb[2]);
    }
    SREG = sreg;
}

#endif
