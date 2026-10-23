/**
 * @file gesture.h
 * @brief Wave / double wave / hold detection from distance readings
 * @details
 * - Background: anything static for GESTURE_BG_LEARN_MS becomes the
 *   background. Only objects GESTURE_BG_MARGIN_MM closer than the background
 *   (and closer than GESTURE_NEAR_MM) count as a hand, so waving between the
 *   lamp and a static object works.
 * - Wave: hand present for GESTURE_WAVE_MIN_MS..GESTURE_WAVE_MAX_MS. Reported
 *   GESTURE_DOUBLE_GAP_MS after the hand left, unless a second wave follows
 *   (then GESTURE_DOUBLE_WAVE).
 * - Hold: hand present longer than GESTURE_WAVE_MAX_MS. GESTURE_HOLD is
 *   reported every tick once the hand moved GESTURE_HOLD_MOVE_MM (a placed
 *   object never does), GESTURE_HOLD_END when it leaves. gesture_mm holds the
 *   distance; for HOLD_END it is the reading from before the hand started
 *   pulling away.
 * - Tracking (gesture_set_tracking(1), e.g. tone setting): no waves or holds,
 *   GESTURE_TRACK every tick while a hand is present, GESTURE_TRACK_END
 *   (rolled back like HOLD_END) when it leaves. After tracking ends, a hand
 *   that is still there is ignored until it leaves.
 *
 * gesture_reading() on every new sensor result, gesture_update() every TICK_MS.
 */

#ifndef GESTURE_H
#define GESTURE_H

#include <stdint.h>
#include "tmf8801.h"

#define GESTURE_NEAR_MM       600    // ignore anything farther (sensor sees ~640 mm)
#define GESTURE_DROPOUT       2      // missed readings in a row before the hand counts as gone
#define GESTURE_BG_MARGIN_MM  80     // hand must be this much closer than the background
#define GESTURE_BG_TOL_MM     30     // readings within this are "not moving"
#define GESTURE_BG_LEARN_MS   10000  // static this long -> becomes background
#define GESTURE_WAVE_MIN_MS   50     // shorter = noise (sensor reports every 33 ms)
#define GESTURE_WAVE_MAX_MS   600    // longer = hold
#define GESTURE_DOUBLE_GAP_MS 500    // max pause between the two waves of a double wave
#define GESTURE_HOLD_MOVE_MM  30     // hold reports only after the hand moved this much
#define GESTURE_HISTORY       6      // readings (~200 ms) rolled back on release

#define GESTURE_BG_LEARN_TICKS  (GESTURE_BG_LEARN_MS / TICK_MS)
#define GESTURE_WAVE_MIN_TICKS  (GESTURE_WAVE_MIN_MS / TICK_MS)
#define GESTURE_WAVE_MAX_TICKS  (GESTURE_WAVE_MAX_MS / TICK_MS)
#define GESTURE_DOUBLE_GAP_TICKS (GESTURE_DOUBLE_GAP_MS / TICK_MS)

typedef enum {
    GESTURE_NONE,
    GESTURE_WAVE,
    GESTURE_DOUBLE_WAVE,
    GESTURE_HOLD,
    GESTURE_HOLD_END,
    GESTURE_TRACK,
    GESTURE_TRACK_END,
} gesture_t;

static uint16_t gesture_mm;  // distance for GESTURE_HOLD / GESTURE_HOLD_END

static uint16_t gesture_bg = TMF8801_NO_TARGET;
static uint16_t gesture_bg_candidate = TMF8801_NO_TARGET;
static uint16_t gesture_bg_ticks;
static uint8_t gesture_present;
static uint8_t gesture_missed;  // readings without a hand since the last one with it
static uint16_t gesture_last_mm;
static uint16_t gesture_hist[GESTURE_HISTORY];
static uint8_t gesture_hist_i;

static uint8_t gesture_near_ticks;  // how long the hand has been present, saturates
static uint8_t gesture_gap_ticks;   // time since a wave ended, 0 = no wave pending
static uint8_t gesture_holding;     // hand moved during hold / present while tracking
static uint16_t gesture_hold_start_mm;
static uint8_t gesture_tracking;
static uint8_t gesture_ignore;      // wait for the hand to leave

static uint16_t gesture_diff(uint16_t a, uint16_t b) {
    return a > b ? a - b : b - a;
}

/**
 * Feed a new sensor reading
 */
static void gesture_reading(uint16_t mm) {
    if (gesture_diff(mm, gesture_bg_candidate) > GESTURE_BG_TOL_MM) {
        gesture_bg_candidate = mm;
        gesture_bg_ticks = 0;
    }

    // mm < NEAR first, so mm + margin can't overflow
    if (mm < GESTURE_NEAR_MM && mm + GESTURE_BG_MARGIN_MM < gesture_bg) {
        gesture_present = 1;
        gesture_missed = 0;
        gesture_last_mm = mm;

        // Only hand readings, so the rollback never lands on "no target"
        gesture_hist[gesture_hist_i] = mm;
        if (++gesture_hist_i == GESTURE_HISTORY) gesture_hist_i = 0;
    } else if (gesture_missed < GESTURE_DROPOUT && ++gesture_missed == GESTURE_DROPOUT) {
        // Bridge single dropouts - readings get unreliable near the range limit
        gesture_present = 0;
    }
}

/**
 * Switch tracking mode, resets wave / hold detection
 */
static void gesture_set_tracking(uint8_t on) {
    gesture_tracking = on;
    gesture_near_ticks = 0;
    gesture_gap_ticks = 0;
    gesture_holding = 0;
    gesture_ignore = !on;
}

/**
 * @return 1 if nothing is going on (no hand, no gesture pending, not tracking)
 */
static uint8_t gesture_idle(void) {
    return !gesture_present && !gesture_near_ticks && !gesture_gap_ticks && !gesture_tracking && !gesture_ignore;
}

/**
 * @return detected gesture, see GESTURE_*
 */
static uint8_t gesture_update(void) {
    if (gesture_bg_ticks < GESTURE_BG_LEARN_TICKS && ++gesture_bg_ticks == GESTURE_BG_LEARN_TICKS) {
        gesture_bg = gesture_bg_candidate;
    }

    if (gesture_ignore) {
        if (gesture_present) return GESTURE_NONE;
        gesture_ignore = 0;
    }

    if (gesture_tracking) {
        if (gesture_present) {
            gesture_holding = 1;
            gesture_mm = gesture_last_mm;
            return GESTURE_TRACK;
        }
        if (gesture_holding) {
            gesture_holding = 0;
            gesture_mm = gesture_hist[gesture_hist_i];  // oldest reading
            return GESTURE_TRACK_END;
        }
        return GESTURE_NONE;
    }

    if (gesture_present) {
        if (gesture_near_ticks < 255) gesture_near_ticks++;
        if (gesture_near_ticks == GESTURE_WAVE_MAX_TICKS + 1) {
            // Hold starts - a wave just before it still counts (wave on, then dim)
            gesture_hold_start_mm = gesture_last_mm;
            if (gesture_gap_ticks) {
                gesture_gap_ticks = 0;
                return GESTURE_WAVE;
            }
        }
        if (gesture_near_ticks > GESTURE_WAVE_MAX_TICKS) {
            if (!gesture_holding && gesture_diff(gesture_last_mm, gesture_hold_start_mm) > GESTURE_HOLD_MOVE_MM) {
                gesture_holding = 1;
            }
            if (gesture_holding) {
                gesture_mm = gesture_last_mm;
                return GESTURE_HOLD;
            }
        }
        return GESTURE_NONE;
    }

    if (gesture_near_ticks) {
        // Hand just left
        uint8_t ticks = gesture_near_ticks;
        gesture_near_ticks = 0;

        if (gesture_holding) {
            gesture_holding = 0;
            gesture_mm = gesture_hist[gesture_hist_i];  // oldest reading
            return GESTURE_HOLD_END;
        }
        if (ticks >= GESTURE_WAVE_MIN_TICKS && ticks <= GESTURE_WAVE_MAX_TICKS) {
            if (gesture_gap_ticks) {
                gesture_gap_ticks = 0;
                return GESTURE_DOUBLE_WAVE;
            }
            gesture_gap_ticks = 1;
        }
        return GESTURE_NONE;
    }

    if (gesture_gap_ticks && ++gesture_gap_ticks > GESTURE_DOUBLE_GAP_TICKS) {
        gesture_gap_ticks = 0;
        return GESTURE_WAVE;
    }
    return GESTURE_NONE;
}

#endif
