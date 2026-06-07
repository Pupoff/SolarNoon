#pragma once
#include <Arduino.h>

// RingBehavior lives here so Arduino IDE's forward-declaration injector
// sees it before generating signatures for ledRingBehavior().
enum RingBehavior {
    SYNC_WITH_KNOB,       // ring tracks the parameter value (knob or MIDI)
    SYNC_WITH_MIDI_CUSTOM // ring driven by ringMidiValues[] independently
};

// Converts user-facing ring ID (1–5) to 0-based internal index.
// Returns -1 on invalid input. Used by knobs and midi_map too.
inline int ringIdx(int id) {
    if (id < 1 || id > 5) return -1;
    return id - 1;
}

// IS31FL3731 linear indices for status LEDs — physical LED colors noted below.
// IS31 is a monochrome driver: it controls brightness only, color is the LED itself.
const int LED_BATTERY_1 = 29; // green — battery bar low
const int LED_BATTERY_2 = 28; // green — battery bar mid
const int LED_BATTERY_3 = 27; // green — battery bar high
const int LED_WIFI       = 30; // blue  — BLE connection status
const int LED_BLE      = 31; // green — WiFi connection status
const int LED_USER2     = 41; // cyan  — user-defined
const int LED_UNUSED     = 40;

// IS31FL3731 three-way position indicator — Matrix B, SW2 row (y=1), x=13..15
// Standard formula: index = y*16 + x  →  y=1: indices 16+x
// Enabled by ENABLE_MASK byte 3 (SW2 CS9-CS16 = 0xFF).
// Ring 5 uses SW1 Matrix B (y=0), so SW2 Matrix B is free for these.
const int LED_3WAY_UP  = 24; // y=1 x=8  (SW2 CS9)
const int LED_3WAY_MID = 25; // y=1 x=9  (SW2 CS10)
const int LED_3WAY_BOT = 26; // y=1 x=10 (SW2 CS11)

// Written by midi_map, read by updateRings()
extern int          ringMidiValues[5];
extern RingBehavior ringBehaviors[5];

bool initLedsMonochrome(); // false if IS31FL3731 not found
void setLedRing(int knobID, int value);                   // knobID 1–5, value 0–255
void setLedRingFill(int knobID, int numLeds, bool rtl);         // numLeds 0-16, binary
void setLedRingFillSmooth(int knobID, int value, bool rtl);         // value 0-255, partial fade
void setLedRingFillSmoothAlt(int knobID, int value, bool rtl);      // second row direction reversed
void setLedRingFillSmoothBidi(int knobID, int value);               // both ends toward center
void ledRingBehavior(int knobID, RingBehavior behavior);
void updateRings();
void syncLedRingWithKnob(int knobID);
void forceRingRedraw(); // redraw all rings with current knob positions (bypasses hysteresis cache)
void setIS31Led(int index, int brightness);   // any IS31 index 0–143
void setStatusLed(int index, int brightness); // convenience alias for indices 85–90
void setUser1Led(bool on);
void setUser1LedAnalog(int val);

void initStatusLeds();
