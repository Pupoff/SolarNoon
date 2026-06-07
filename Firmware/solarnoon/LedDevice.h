#pragma once
#include <Arduino.h>
#include "pin.h"
#include "leds_mono.h"

enum LedDeviceType {
    LED_DEVICE_RING,  // full LED ring,             id = knobID 1–5 (user-facing)
    LED_DEVICE_IS31,  // single IS31FL3731 LED,     id = linear index 0–143
    LED_DEVICE_GPIO,  // direct GPIO LED,            id = pin number
    LED_DEVICE_RGB    // WS2812B strip LED,          id = strip index (LED_SWITCH_A …)
};

struct LedDevice {
    LedDeviceType type;
    int id;
};

// Factory helpers — use these as the first argument of mapLEDtoMIDI()
//   RingLED(1–5)                    — a full 16-LED ring
//   UserLed(1–2)                 — user-assignable LEDs (hides GPIO vs IS31 detail)
//   StatusLed(LED_BLE / LED_WIFI / LED_BATTERY_x) — status indicator LEDs
inline LedDevice RingLED(int knobID)  { return {LED_DEVICE_RING, knobID}; }
inline LedDevice StatusLed(int index) { return {LED_DEVICE_IS31, index};  }
inline LedDevice UserLed(int id) {
    if (id == 1) return {LED_DEVICE_GPIO, PIN_LED_USER1}; // pink, GPIO 13
    if (id == 2) return {LED_DEVICE_IS31, LED_USER2};     // cyan, IS31 index 90
    Serial.println("UserLed: id out of range (1–2)");
    return {LED_DEVICE_GPIO, -1};
}
inline LedDevice RGBLed(int stripIdx)  { return {LED_DEVICE_RGB, stripIdx}; }