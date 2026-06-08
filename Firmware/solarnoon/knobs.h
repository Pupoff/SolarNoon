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
#include <Arduino.h>

void initKnobs();
int  readKnob(int knobID);            // knobID 1–5, returns 0–4095

// Knob → fixed CC, direct "send on change" mapping with none of the
// effectControls[][] bookkeeping (no Parameter, no MIDI feedback, no "?" —
// just a raw 0-127 number sent straight to a CC each time it moves). Mirrors
// the value onto ringMidiValues[ringIdx] at the same scale and flags a
// redraw on change. `lastSent` is the caller's own per-knob "last value
// sent" storage (seed it to -1 to force an initial send) — this is a
// per-tick action, not a persistent binding, so it has nothing to release
// when you stop calling it. See "Solar System" walkthrough, GETTING_STARTED.md,
// for the block this replaces and why mode 4 doesn't use effectControls[][].
void driveKnobToCC(int knobNum, int ringIdx, uint8_t cc, int& lastSent);
void setKnob3EncoderPos(int v4095);   // KNOB3_IS_ENDLESS only: seed accumulator position
int  getKnob3RawDial();               // KNOB3_IS_ENDLESS only: current dial pos 0..ENC3_DIAL_RANGE-1
void debugPrintKnobs();               // prints all 5 knob raw values to Serial
void updateKnobsMidi();

#define ENC3_DIAL_RANGE 256           // resolution of the atan2 dial circle


