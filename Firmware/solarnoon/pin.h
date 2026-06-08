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

// Battery ADC, BQ24074 OUT → divider R1=450k/R2=100k → IO6
#define PIN_BATT_ADC    6

// BLE on/off switch, LOW = BLE OFF, HIGH = BLE ON
#define PIN_BLE_SW 42

// Knob 3, uncomment to use RV112FF endless pot instead of the analog pot.
// Track A stays on KNOB_PINS[2] (GPIO4). Track B (second wiper) on IO2.
// If rotation direction is wrong, swap the two wiper wires on the PCB.
#define KNOB3_IS_ENDLESS
#define PIN_ENC3_B 2

// ── I2C (IS31FL3731 LED driver) ───────────────────────────────────────────────

#define PIN_I2C_SDA 17
#define PIN_I2C_SCL 18

// ── RGB LEDs (WS2812B, IO47) ──────────────────────────────────────────────────

#define PIN_RGB_DATA    47


// ── OLED display (SH1106G, SPI) ───────────────────────────────────────────────

#define OLED_CS            40 // Base: 9
#define OLED_DC            16 // Base: 10
#define OLED_MOSI          11
#define OLED_SCK           12
#define OLED_RST           21


// ── Status LEDs ───────────────────────────────────────────────────────────────

// Direct GPIO LED
#define PIN_LED_USER1  13 // pink


// ── Three-way momentary switch ────────────────────────────────────────────────

#define PIN_3WAY_DOWN   39 // Base: 45  // IO45 SW4:momentary, moves position up
#define PIN_3WAY_UP 48  // IO48 SW4:momentary, moves position down



// ── Switches ─────────────────────────────────────────────────────────────────

#define PIN_SW_A          38 // Base: 35  // SW1: SW_A
#define PIN_SW_B           3 // Base:4  // SW2: SW_B
#define PIN_SW_C           5  // SW3: SW_C
#define PIN_EFFECT_SWITCH  14 // Base: 7  // SW6: cycles through effects
#define PIN_DISPLAY_SWITCH 15  // SW5: cycles through display screens


// Knob pins, internal 0-based index maps to user-facing ring ID 1-5
const int KNOB_PINS[5] = {7,9,4,8,10};//base: {14, 46, 3, 8, 16};
