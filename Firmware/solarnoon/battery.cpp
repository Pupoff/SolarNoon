// midi_a14h - MIDI controller firmware
// Copyright (C) 2026 Maxime Popoff
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "battery.h"
#include "pin.h"
#include "leds_mono.h"
#include "leds_rgb.h"
#include "display.h"
#include <Arduino.h>
#include <esp_sleep.h>

// BQ24074 OUT ≈ V_LIPO (power-path FET).
// Divider: R1=450k , R2=100k  → V_IO6 = V_BAT / 5.5
//  Seems that Vout is almost the same as Vbatt

// ADC_11db default: 0–3.3 V full scale (12-bit).
static constexpr float BATT_VREF  = 3.3f;
static constexpr float R1 = 450000.0f;
static constexpr float R2 = 100000.0f;

static constexpr float BATT_V_STEP1 = 3.2f; 
static constexpr float BATT_V_STEP2 = 3.4f; 
static constexpr float BATT_V_STEP3 = 3.8f; 

static constexpr int   BATT_SAMPLES = 16;

static float cachedVoltage = 0.0f;

void initBattery() {
    pinMode(PIN_BATT_ADC, INPUT);
    analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
    // Prime cache immediately so first display draw has real data.
    long sum = 0;
    for (int i = 0; i < BATT_SAMPLES; i++) sum += analogReadMilliVolts(PIN_BATT_ADC)+25;
    cachedVoltage = (sum / (float)BATT_SAMPLES)/1000 * (R1+R2)/R2; 
}

float getBatteryVoltage() { return cachedVoltage; }

// ── Shutdown on critically low battery ───────────────────────────────────────
// Voltage must stay below BATT_SHUTDOWN_V for BATT_SHUTDOWN_MS before we act,
// to avoid triggering on momentary load-induced dips.
static constexpr float        BATT_SHUTDOWN_V  = 3.1f;
static constexpr unsigned long BATT_SHUTDOWN_MS = 5000;

void checkBatteryShutdown() {
    if (cachedVoltage < 0.5f) return; // no valid reading yet

    static unsigned long belowSince = 0;

    if (cachedVoltage >= BATT_SHUTDOWN_V) {
        belowSince = 0;
        return;
    }

    if (belowSince == 0) { belowSince = millis(); return; }
    if (millis() - belowSince < BATT_SHUTDOWN_MS) return;

    // Confirmed critically low, shut everything down
    drawScreenBatteryDead();

    for (int r = 1; r <= 5; r++) setLedRing(r, 0);
    for (int i = 0; i < RGB_RING_SIZE; i++) setRGBledRaw(RGB_RING_START + i, CRGB::Black);
    setRGBledRaw(LED_EFFECT,   CRGB::Black);
    setRGBledRaw(LED_SWITCH_A, CRGB::Black);
    setRGBledRaw(LED_SWITCH_B, CRGB::Black);
    setRGBledRaw(LED_SWITCH_C, CRGB::Black);
    setRGBledRaw(LED_MODE,     CRGB::Black);
    showRGBLeds();

    delay(3000); // let the user read the screen
    esp_deep_sleep_start();
}

void updateBatteryLeds() {
    static unsigned long lastRead  = 0;
    static unsigned long lastBlink = 0;
    static bool          blinkOn   = true;

    unsigned long now = millis();

    // Re-read ADC every 5 s.
    if (now - lastRead >= 5000) {
        lastRead = now;
        long sum = 0;
    for (int i = 0; i < BATT_SAMPLES; i++) sum += analogReadMilliVolts(PIN_BATT_ADC)+25;
    cachedVoltage = (sum / (float)BATT_SAMPLES)/1000 * (R1+R2)/R2; 
    }

    // Blink LED 1 at 1 Hz when critically low (< 10 %).
    bool critical = (cachedVoltage < BATT_V_STEP1);
    if (critical) {
        if (now - lastBlink >= 500) { blinkOn = !blinkOn; lastBlink = now; }
    } else {
        blinkOn = true;
    }

    // Three-bar indicator, each LED is fully on or off.
    // < 10 %: LED1 blinks.  10-32 %: LED1.  33-65 %: LED1+2.  66-100 %: all 3.
    int b1 = (cachedVoltage > 0 && (!critical || blinkOn)) ? 255 : 0;
    int b2 = (cachedVoltage >= BATT_V_STEP2) ? 255 : 0;
    int b3 = (cachedVoltage >= BATT_V_STEP3) ? 255 : 0;

    setIS31Led(LED_BATTERY_1, b1);
    setIS31Led(LED_BATTERY_2, b2);
    setIS31Led(LED_BATTERY_3, b3);
}
