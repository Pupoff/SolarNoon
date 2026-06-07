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

void initMidi();
void updateMidi();      // call in loop() -- runs Control_Surface.loop()
void buildMidiLookup(); // called by initMidi(); can be re-called after parameter changes

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
