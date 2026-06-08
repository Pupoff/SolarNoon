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

#pragma once
#include <stdint.h>

struct BoardSettings {
    uint8_t ringMonoBrightness;    // 0-100, IS31 ring LEDs (PWM level)
    uint8_t rgbRingBrightness;     // 0-100, RGB ring LEDs
    uint8_t rgbSwitchBrightness;   // 0-100, footswitch + mode/effect RGB LEDs
    uint8_t displayContrast;       // 0-100, OLED contrast
    bool    rgbRingEnabled;        // RGB ring on/off
    uint8_t layoutId;              // 1=classic list, 2=5-column, 3=2+3 grid
    uint8_t deviceMode;            // 1=white 2=orange 3=red (LED_MODE color)
    uint8_t enc3Sensitivity;       // 0-100, endless pot sensitivity (100=fastest, 0=slowest)
};

extern BoardSettings boardSettings;

void loadSettings();  // load from NVS flash (call before initDisplay)
void saveSettings();  // persist to NVS flash
