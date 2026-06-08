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

#include "direct_controls.h"
#include "knobs.h"            // readKnob()
#include "leds_mono.h"        // ringMidiValues[]
#include "three_way_switch.h" // threeWayPosition
#include "midi.h"             // sendMidiCC(), sendMidiSwitch(), displayRefreshNeeded

bool buttonPressed(int pin, ButtonEdge& state) {
    bool reading = digitalRead(pin);
    if (reading != state.lastReading) {
        state.lastChange  = millis();
        state.lastReading = reading;
    }
    if (millis() - state.lastChange >= 50 && reading != state.lastStable) {
        state.lastStable = reading;
        return reading == LOW;
    }
    return false;
}

void driveKnobFromControl(int knobNum, Parameter& p) {
    static int lastSent[6] = {-1, -1, -1, -1, -1, -1}; // indices 1-5, [0] unused
    int value = map(readKnob(knobNum), 0, 4095, 0, 127);
    ringMidiValues[knobNum - 1] = value * 255 / 127;
    if (value == lastSent[knobNum]) return;
    lastSent[knobNum] = value;
    p.value = value * 100 / 127;
    p.known = true;
    sendMidiCC(p.midiNote, value);
    displayRefreshNeeded = true;
}

void driveToggleFromControl(Parameter& p) {
    p.value ^= 1;
    p.known  = true;
    sendMidiSwitch(p.midiNote, p.value);
    displayRefreshNeeded = true;
}

void driveSwitchFromControl(int pin, ButtonEdge& state, Parameter& p) {
    if (buttonPressed(pin, state)) driveToggleFromControl(p);
}

void drive3waySwitchFromControl(Parameter& p, bool resync) {
    static int lastPos = -1;
    if (resync) {
        threeWayPosition = p.value ? 2 : 0;
        lastPos          = threeWayPosition;
        return;
    }
    if (threeWayPosition == lastPos) return;
    lastPos   = threeWayPosition;
    int value = (threeWayPosition > 0) ? 1 : 0;
    p.value = value;
    p.known = true;
    sendMidiSwitch(p.midiNote, value);
    displayRefreshNeeded = true;
}
