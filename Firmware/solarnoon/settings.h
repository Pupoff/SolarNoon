#pragma once
#include <stdint.h>

struct BoardSettings {
    uint8_t ringMonoBrightness;    // 0-100 — IS31 ring LEDs (PWM level)
    uint8_t rgbRingBrightness;     // 0-100 — RGB ring LEDs
    uint8_t rgbSwitchBrightness;   // 0-100 — footswitch + mode/effect RGB LEDs
    uint8_t displayContrast;       // 0-100 — OLED contrast
    bool    rgbRingEnabled;        // RGB ring on/off
    uint8_t layoutId;              // 1=classic list, 2=5-column, 3=2+3 grid
    uint8_t deviceMode;            // 1=white 2=orange 3=red (LED_MODE color)
    uint8_t enc3Sensitivity;       // 0-100 — endless pot sensitivity (100=fastest, 0=slowest)
};

extern BoardSettings boardSettings;

void loadSettings();  // load from NVS flash (call before initDisplay)
void saveSettings();  // persist to NVS flash
