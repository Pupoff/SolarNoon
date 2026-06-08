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
#include "parameters.h"
#include "LedDevice.h"

// ── MIDI control descriptor ───────────────────────────────────────────────────

struct MidiControl {
    uint8_t channel; // 1–16
    uint8_t number;  // CC number
    // TODO: add type field (CC / Note / etc.)
};

// ── LED-to-MIDI mapping storage ───────────────────────────────────────────────
// One entry per mapped non-ring LED. Rings use ringMidiMap[] / ringBehaviors[].
// value: last received MIDI CC (0-127). Updated by MIDI receiver (TODO).
//        Initialized to 0 (off/dim) when the mapping is registered.

#define MAX_LED_ENTRIES 6

struct LedMidiEntry {
    LedDevice   device;
    MidiControl control;
    RGBColor    onColor; // RGB: full-bright color when CC > 63
                         // IS31/GPIO: ignored (fixed brightness / HIGH)
    int         value;   // 0-127, starts at 0
    bool        valid;
};

extern MidiControl  ringMidiMap[5];
extern LedMidiEntry ledMidiEntries[MAX_LED_ENTRIES];
extern int          ledMidiEntryCount;
extern bool         displayRefreshNeeded; // set by callback; checked in loop()

// ── Functions ─────────────────────────────────────────────────────────────────

struct BlePeerInfo {
    char addr[18]; // "AA:BB:CC:DD:EE:FF", or "---" when not connected
    bool bonded;   // true after pairing (security keys exchanged and stored)
    bool encrypted;
};

bool isMidiMounted();   // true when USB host has enumerated (USB) or BLE central is connected
bool getFeedbackOk();   // true if a CC was received back after the last send (within 3 s)
bool isBleMode();         // true when booted with BLE switch ON
void disableBle();        // silence BLE sends mid-session (activeMidi → nullptr); reboot for USB
int         getBleBondCount();  // number of bonded BLE devices; -1 when in USB mode
BlePeerInfo getBlePeerInfo();   // peer address + bonded/encrypted state; addr="---" if not connected
void updateBleStatusLeds();     // LED_WIFI/LED_BLE indicators + boot-transport-mismatch warning, call every loop()

void initMidi();
void updateMidi();      // call in loop() -- runs Control_Surface.loop()
void buildMidiLookup(); // called by initMidi(); can be re-called after parameter changes

// ── Why rings can be rebound in loop() but other LEDs can't ──────────────────
// For StatusLed/UserLed/RGBLed, mapLEDtoMIDI() appends a new entry to the
// fixed-size ledMidiEntries[MAX_LED_ENTRIES] array (storeLedEntry, see
// midi.cpp), which the MIDI receive callback then scans to drive that LED.
// There's no update or remove: call it more than once for the same LED, e.g.
// every loop() tick, or once per page, and you just keep appending duplicate
// entries until the array fills up and further calls are silently dropped.
//
// LED_DEVICE_RING is the exception: its mapping isn't stored as an entry in
// that array at all. mapLEDtoMIDI() instead writes straight into that ring's
// own fixed slot (ringMidiMap[i] / ringMidiValues[i] / ringBehaviors[i]),
// overwriting whatever was there before. So calling it again just replaces
// the previous binding in place, safe to do repeatedly, including rebinding
// a ring to a different CC depending on which page is active.

// For rings, IS31, GPIO -- no color needed
void mapLEDtoMIDI(LedDevice device, MidiControl control);

// For RGB strip LEDs -- onColor is the full-bright color when CC > 63;
// off state shines the same color at 1/16 brightness
void mapLEDtoMIDI(LedDevice device, MidiControl control, RGBColor onColor);

void sendMidiCC(uint8_t ccNumber, uint8_t value);

// Sends an on/off switch change for `newValue` (the new local state, 0 or 1),
// translating it to whatever the plugin expects per SWITCH_MIDI_MODE
// (parameters.h): absolute on/off value, or a fixed toggle trigger.
void sendMidiSwitch(uint8_t ccNumber, uint8_t newValue);
