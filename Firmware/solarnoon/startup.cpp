#include "startup.h"
#include "leds_mono.h"
#include "leds_rgb.h"
#include "effects.h"

// LED 0 = right (3h), CCW → LED 6 = bottom (6h), LED 12 = left (9h), LED 18 = top (12h)
#define BOTTOM_LED      18
#define SUNRISE_STEPS  80
#define SUNRISE_MS     1 

// Sunrise palette: step 0=black → step SUNRISE_STEPS=warm amber (255,180,30)
//   0   → /2   : black  → full red    (255,   0,  0)
//   /2  → 3/4  : red    → orange      (255, 120,  0)
//   3/4 → full : orange → warm amber  (255, 180, 30)
static CRGB sunriseColor(int step) {
    const int N = SUNRISE_STEPS;
    if (step <= 0)   return CRGB::Black;
    if (step >= N)   return CRGB(255, 180, 30);
    if (step < N / 2) {
        return CRGB((uint8_t)((long)step * 510 / N), 0, 0);
    }
    if (step < N * 3 / 4) {
        int s = step - N / 2, span = N / 4;
        return CRGB(255, (uint8_t)((long)s * 120 / span), 0);
    }
    int s = step - N * 3 / 4, span = N / 4;
    return CRGB(255,
                (uint8_t)(120 + (long)s * 60 / span),
                (uint8_t)((long)s * 30 / span));
}

// One animation frame at sunrise step s (0..SUNRISE_STEPS).
static void animFrame(int step) {
    // ── RGB ring: arc spreading from BOTTOM_LED outward ───────────────────────
    // Fixed-point: litTotal / SUNRISE_STEPS = fully lit radius; frac = partial edge.
    int  litTotal   = step * (RGB_RING_SIZE / 2 + 1);
    int  litFull    = litTotal / SUNRISE_STEPS;
    int  litFrac    = litTotal % SUNRISE_STEPS;
    CRGB c          = sunriseColor(step);
    CRGB cPartial   = CRGB((uint8_t)((long)c.r * litFrac / SUNRISE_STEPS),
                           (uint8_t)((long)c.g * litFrac / SUNRISE_STEPS),
                           (uint8_t)((long)c.b * litFrac / SUNRISE_STEPS));
    for (int i = 0; i < RGB_RING_SIZE; i++) {
        int d    = abs(i - BOTTOM_LED);
        int dist = min(d, RGB_RING_SIZE - d);
        setRGBledRaw(RGB_RING_START + i, dist < litFull  ? c
                                       : dist == litFull ? cPartial
                                       : CRGB::Black);
    }
    showRGBLeds();

    // ── IS31 rings: smooth fill proportional to step ─────────────────────────
    int ringVal = step * 255 / SUNRISE_STEPS;
    setLedRingFillSmoothBidi(3, ringVal);            // centre:    both ends → top
    setLedRingFillSmooth(4, ringVal, false);         // knobs 4&5: left→right
    setLedRingFillSmooth(5, ringVal, false);
    setLedRingFillSmoothAlt(1, ringVal, true);       // knobs 1&2: right→left, row 2 flipped
    setLedRingFillSmoothAlt(2, ringVal, true);
}

void startupAnimation() {
    // ── Sunrise fill — 2 s ───────────────────────────────────────────────────
    for (int s = 0; s <= SUNRISE_STEPS; s++) {
        animFrame(s); delay(SUNRISE_MS); 
        setIS31Led(LED_USER2, s*255/SUNRISE_STEPS);
        setUser1LedAnalog(s*255/SUNRISE_STEPS);
        }
    // ── Hold fully lit ────────────────────────────────────────────────────────
    delay(600);

    // ── All off ───────────────────────────────────────────────────────────────
    for (int r = 1; r <= 5; r++) setLedRing(r, 0);
    setRGBledRaw(RGB_RING, CRGB::Black);
    setUser1LedAnalog(0); // stop LEDC channel that was used during animation ramp
    setIS31Led(LED_USER2, 0);
    showRGBLeds();

    delay(300);


}
