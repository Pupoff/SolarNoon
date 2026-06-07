#include "leds_mono.h"
#include "debug.h"
#include "knobs.h"    // for readKnob()
#include "pin.h"
#include "settings.h" // boardSettings
#include "effects.h"  // effectSections, currentEffectIdx, currentMode, getKnobParamIdx, parameters
#include "display.h"  // currentScreen
#include <Adafruit_IS31FL3731.h>
#include <Wire.h>

static Adafruit_IS31FL3731 ledRing;

RingBehavior ringBehaviors[5];
int          ringMidiValues[5] = {0, 0, 0, 0, 0};

static int lastRing[5] = {-1,-1,-1,-1,-1};

// Adafruit begin() enables all 144 LEDs by default.
// Unconnected LEDs cause crosstalk — must be explicitly disabled.
//
// Physical wiring:
//   Matrix A (CS1-CS8):  all 9 rows fully connected
//   Matrix B (CS9-CS16): SW1 all 8, SW2 all 8, SW3 CS9+CS10 only, rest none
static const uint8_t ENABLE_MASK[18] = {
    0xFF, 0xFF, // SW1: A=all, B=all
    0xFF, 0xFF, // SW2: A=all, B=all
    0xFF, 0x03, // SW3: A=all, B=CS9+CS10 only
    0xFF, 0x00, // SW4: A=all, B=none
    0xFF, 0x00, // SW5: A=all, B=none
    0xFF, 0x00, // SW6: A=all, B=none
    0xFF, 0x00, // SW7: A=all, B=none
    0xFF, 0x00, // SW8: A=all, B=none
    0xFF, 0x00, // SW9: A=all, B=none
};

static void applyEnableMask() {
    for (uint8_t frame = 0; frame < 8; frame++) {
        Wire.beginTransmission(0x74);
        Wire.write(0xFD);
        Wire.write(frame);
        Wire.endTransmission();
        Wire.beginTransmission(0x74);
        Wire.write(0x00);
        for (int i = 0; i < 18; i++) Wire.write(ENABLE_MASK[i]);
        Wire.endTransmission();
    }
}

bool initLedsMonochrome() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    if (!ledRing.begin()) {
        DBG_PRINTLN("IS31FL3731 not found");
        return false;
    }
    applyEnableMask();
    return true;
}

void setLedRing(int knobID, int value) {
    int i = ringIdx(knobID);
    if (i < 0) { DBG_PRINTLN("setLedRing: knobID out of range (1–5)"); return; }
    value = constrain(value, 0, 255);

    // Smooth fill: LEDs 0..fullLeds-1 at full brightness,
    // LED fullLeds fades in proportionally (no hard snap at threshold).
    int total    = value * 16;          // fixed-point 0..4080
    int fullLeds = total / 255;         // 0..16 fully lit LEDs
    int rem      = total % 255;         // fractional portion 0..254

    uint8_t fullBright    = (uint8_t)(255 * boardSettings.ringMonoBrightness / 100);
    uint8_t partialBright = (uint8_t)((long)rem * boardSettings.ringMonoBrightness / 100);

    for (int j = 0; j < 16; j++) {
        int x, y;
        if (i < 4) {
            x = j % 8;
            y = i * 2 + j / 8;
        } else {
            if (j < 8) { x = j; y = 8; }
            else        { x = j; y = 0; }
        }
        uint8_t bright = (j < fullLeds) ? fullBright
                       : (j == fullLeds) ? partialBright
                       : 0;
            ledRing.drawPixel(x, y, bright);
    }
}

// Directional fill: numLeds 0-16, rtl=true → right-to-left fill order.
// Uses the same brightness as setLedRing().  Works for all 5 rings.
void setLedRingFill(int knobID, int numLeds, bool rtl) {
    int i = ringIdx(knobID);
    if (i < 0) { DBG_PRINTLN("setLedRingFill: knobID out of range"); return; }
    numLeds = constrain(numLeds, 0, 16);
    uint8_t bright = (uint8_t)(255 * boardSettings.ringMonoBrightness / 100);
    for (int j = 0; j < 16; j++) {
        int x, y;
        if (i < 4) {
            x = rtl ? (7 - j % 8) : (j % 8);
            y = i * 2 + j / 8;
        } else {
            // Ring 5: j<8 → y=8 x=0..7, j>=8 → y=0 x=8..15
            if (j < 8) { x = rtl ? (7 - j)   : j;       y = 8; }
            else        { x = rtl ? (23 - j)  : j;       y = 0; }
        }
        ledRing.drawPixel(x, y, j < numLeds ? bright : 0);
    }
}

// Smooth directional fill: value 0-255, partial next LED fades in.
void setLedRingFillSmooth(int knobID, int value, bool rtl) {
    int i = ringIdx(knobID);
    if (i < 0) { DBG_PRINTLN("setLedRingFillSmooth: knobID out of range"); return; }
    value = constrain(value, 0, 255);
    int total    = value * 16;
    int fullLeds = total / 255;
    int rem      = total % 255;
    uint8_t fullBright    = (uint8_t)(255 * boardSettings.ringMonoBrightness / 100);
    uint8_t partialBright = (uint8_t)((long)rem * boardSettings.ringMonoBrightness / 100);
    for (int j = 0; j < 16; j++) {
        int x, y;
        if (i < 4) {
            x = rtl ? (7 - j % 8) : (j % 8);
            y = i * 2 + j / 8;
        } else {
            if (j < 8) { x = rtl ? (7 - j)  : j;    y = 8; }
            else        { x = rtl ? (23 - j) : j;    y = 0; }
        }
        uint8_t bright = (j < fullLeds) ? fullBright
                       : (j == fullLeds) ? partialBright
                       : 0;
        ledRing.drawPixel(x, y, bright);
    }
}

// Continuous sweep through the physical ring top.
// For rings 1&2 with rtl=true: fills 3h→12h→9h without jumping back.
// k=(j<8)?(j+8):(15-j) maps j-index to physical fill order (k=0 = 3h, k=15 = 9h).
void setLedRingFillSmoothAlt(int knobID, int value, bool rtl) {
    int i = ringIdx(knobID);
    if (i < 0) { DBG_PRINTLN("setLedRingFillSmoothAlt: out of range"); return; }
    value = constrain(value, 0, 255);
    int total    = value * 16;
    int fullLeds = total / 255;
    int rem      = total % 255;
    uint8_t fullBright    = (uint8_t)(255 * boardSettings.ringMonoBrightness / 100);
    uint8_t partialBright = (uint8_t)((long)rem * boardSettings.ringMonoBrightness / 100);
    for (int j = 0; j < 16; j++) {
        int k = (j < 8) ? (j + 8) : (15 - j);  // physical fill order: 3h→12h→9h
        uint8_t bright = (k < fullLeds) ? fullBright
                       : (k == fullLeds) ? partialBright
                       : 0;
        int x, y;
        if (i < 4) {
            if (j < 8) x = rtl ? (7 - j) : j;
            else        x = rtl ? (j - 8) : (15 - j);
            y = i * 2 + j / 8;
        } else {
            if (j < 8) { x = rtl ? (7 - j)  : j;    y = 8; }
            else        { x = rtl ? (23 - j) : j;    y = 0; }
        }
        ledRing.drawPixel(x, y, bright);
    }
}

// Smooth bidirectional fill: both ends fill simultaneously toward the center.
// j=0 and j=15 light first; j=7 and j=8 (top/center) light last.
void setLedRingFillSmoothBidi(int knobID, int value) {
    int i = ringIdx(knobID);
    if (i < 0) { DBG_PRINTLN("setLedRingFillSmoothBidi: out of range"); return; }
    value = constrain(value, 0, 255);
    int pairsTotal = value * 8;           // fixed-point: 8 pairs × 255 range
    int pairsFull  = pairsTotal / 255;    // 0..8 fully lit pairs
    int pairsRem   = pairsTotal % 255;
    uint8_t fullBright    = (uint8_t)(255 * boardSettings.ringMonoBrightness / 100);
    uint8_t partialBright = (uint8_t)((long)pairsRem * boardSettings.ringMonoBrightness / 100);
    for (int j = 0; j < 16; j++) {
        int dist = min(j, 15 - j);        // 0 = edge, 7 = center
        uint8_t bright = (dist < pairsFull) ? fullBright
                       : (dist == pairsFull) ? partialBright
                       : 0;
        int x, y;
        if (i < 4) {
            x = j % 8;
            y = i * 2 + j / 8;
        } else {
            if (j < 8) { x = j; y = 8; }
            else        { x = j; y = 0; }
        }
        ledRing.drawPixel(x, y, bright);
    }
}

void ledRingBehavior(int knobID, RingBehavior behavior) {
    int i = ringIdx(knobID);
    if (i < 0) { DBG_PRINTLN("ledRingBehavior: knobID out of range (1–5)"); return; }
    ringBehaviors[i] = behavior;
}

void updateRings() {
    static unsigned long lastRun = 0;
    if (millis() - lastRun < 20) return;
    lastRun = millis();
    for (int i = 0; i < 5; i++) {
        switch (ringBehaviors[i]) {
            case SYNC_WITH_KNOB: {
                // Ring reflects the parameter value for this slot,
                // whether it was set by the physical knob or by an incoming MIDI CC.
                int val = 0;
                if (currentMode != 2 && currentMode != 3) {
                    int effect   = effectSections[currentEffectIdx];
                    int paramIdx = getKnobParamIdx(effect, currentScreen - 1, i);
                    if (paramIdx >= 0)
                        val = max(0, parameters[effect][paramIdx].value - 5) * 255 / 100;
                }
                if (lastRing[i] < 0 || val != lastRing[i]) {
                    lastRing[i] = val;
                    setLedRing(i + 1, val);
                }
                break;
            }
            case SYNC_WITH_MIDI_CUSTOM:
                setLedRing(i + 1, ringMidiValues[i]);
                break;
        }
    }
}

void forceRingRedraw() {
    for (int i = 0; i < 5; i++) lastRing[i] = -1; // invalidate cache → updateRings() will redraw
}

void syncLedRingWithKnob(int knobID) {
    int i = ringIdx(knobID);
    if (i < 0) { DBG_PRINTLN("syncLedRingWithKnob: out of range"); return; }
    // Hysteresis: 1 LED ≈ 16 units, threshold of 4 prevents sub-LED flickering.
    int value = map(readKnob(knobID), 0, 4095, 0, 255);
    if (lastRing[i] < 0 || abs(value - lastRing[i]) > 4) {
        lastRing[i] = value;
        setLedRing(knobID, value);
    }
}

void setIS31Led(int index, int brightness) {
    if (index < 0 || index > 143) { DBG_PRINTLN("setIS31Led: index out of range (0–143)"); return; }
    // Standard Adafruit IS31FL3731 mapping: index = y*16 + x
    ledRing.drawPixel(index % 16, index / 16, constrain(brightness, 0, 255));
}

void setStatusLed(int index, int brightness) {
    setIS31Led(index, brightness);
}

void setUser1Led(bool on) {
    digitalWrite(PIN_LED_USER1, on ? HIGH : LOW);
}
void setUser1LedAnalog(int val) {
    analogWrite(PIN_LED_USER1, val);
}
void initStatusLeds() {
    pinMode(PIN_LED_USER1, OUTPUT);
    setUser1LedAnalog(0); // use PWM path to properly clear any LEDC channel
    setStatusLed(LED_BATTERY_1, 0);
    setStatusLed(LED_BATTERY_2, 0);
    setStatusLed(LED_BATTERY_3, 0);
    setStatusLed(LED_BLE,       0);
    setStatusLed(LED_WIFI,      0);
    setStatusLed(LED_USER2,     0);
}
