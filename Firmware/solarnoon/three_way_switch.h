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

#include <Arduino.h> // uint8_t

extern int threeWayPosition; // 0 = up, 1 = middle, 2 = bottom

void initThreeWaySwitch();
void updateThreeWaySwitch();
void updateSwitchMidi(); // reads threeWayPosition, sends MIDI for switch on current page

// readKnob()-style getter: current debounced position (0=up, 1=mid, 2=bottom),
// kept up to date once per tick by updateThreeWaySwitch(). Prefer this over
// reading threeWayPosition directly from outside this file, same spirit as
// readKnob() hiding the raw-ADC/hysteresis bookkeeping behind a plain getter.
int readThreeWaySwitch();

// Per-tick action, same spirit as driveKnobToCC(): reads the switch's
// position (0=up, 1=mid, 2=bottom), maps it through a fixed 3-entry lookup
// table to a CC value, and sends `cc` only when that value changes from
// `lastSent` — flagging displayRefreshNeeded so anything drawing the
// position (e.g. a moving star) redraws right when it moves. Not a
// permanent binding, just a one-shot-per-tick "drive this CC from the
// switch" helper to call from your mode's updateModeIO() case.
void drive3waySwitchToCC(uint8_t cc, int& lastSent);

// Lights whichever of LED_3WAY_UP/MID/BOT matches `position` (0=up, 1=mid,
// 2=bottom; any other value, e.g. -1, turns all three off) and the other two
// off. Generalizes the switch's own position indicator so a self-contained
// mode can drive it directly — e.g. to show a value it owns instead of the
// physical switch's position, or to blank it while it means nothing on the
// current page (see updateThreeWaySwitch()'s "disabled" branches for examples
// of the all-off case, now just setThreeWaySwitchLeds(-1)).
void setThreeWaySwitchLeds(int position);
