#pragma once

// Battery ADC — BQ24074 OUT → divider R1=450k/R2=100k → IO6
#define PIN_BATT_ADC    6

// BLE on/off switch — LOW = BLE OFF, HIGH = BLE ON
#define PIN_BLE_SW 42

// Knob 3 — uncomment to use RV112FF endless pot instead of the analog pot.
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


// Knob pins — internal 0-based index maps to user-facing ring ID 1-5
const int KNOB_PINS[5] = {7,9,4,8,10};//base: {14, 46, 3, 8, 16};
