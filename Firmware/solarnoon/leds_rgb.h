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
#include <FastLED.h>

// WS2812B strip layout (IO47):
//   [0] SWITCH_A  [1] SWITCH_B  [2] SWITCH_C  [3] EFFECT  [4] MODE  [5–28] RING
const int LED_SWITCH_A = 0;
const int LED_SWITCH_B = 1;
const int LED_SWITCH_C = 2;
const int LED_EFFECT   = 3;
const int LED_MODE     = 4;
const int RGB_RING      = -1; // sentinel: set all 24 ring LEDs
const int RGB_RING_START =  5;
const int RGB_RING_SIZE  = 24;
const int NUM_RGB_LEDS   = 29;

void initRGBLeds();
void setRGBled      (int led, CRGB color); // set + FastLED.show()
void setRGBledRaw   (int led, CRGB color); // set without show, batch multiple calls then showRGBLeds()
void showRGBLeds    ();                    // push buffered changes to strip
void updateLedMidiFeedback(); // applies on/dim color to RGB-mapped LEDs based on stored MIDI values
