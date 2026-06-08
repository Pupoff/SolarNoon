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

#define CC_SW_A 73  // MIDI CC sent by SW_A (IO37)
#define CC_SW_B 74  // MIDI CC sent by SW_B (IO4)
#define CC_SW_C 75  // MIDI CC sent by SW_C (IO5)
// Value: 127 on press, 0 on release

// Hold SW_A and tap SW_B/SW_C to cycle the display mode forward/backward,
// instead of either switch's normal per-mode action/MIDI. Comment out to
// disable the combo and get plain independent switches back.
#define SWITCH_COMBO_MODE_CHANGE

// CC each footswitch currently sends, switchCC[0]=A, [1]=B, [2]=C.
// Assigned explicitly in setup() (see CC_SW_A/B/C above for the defaults);
// plain data, so it's safe to overwrite per page in loop() if you want a
// switch to send a different CC depending on the active screen.
extern uint8_t switchCC[3];

// Lets the active mode take footswitch A over entirely (read/debounce it
// itself and decide what a press does, e.g. toggle a parameter directly
// instead of sending switchCC[0]'s momentary pulse — see
// archetypeVSTglobalcontrols, midi_a14h.ino). Defaults to false (generic
// behavior); a self-contained mode sets it every tick it wants ownership,
// updateModeIO() resets it before dispatching so it auto-releases the moment
// that mode stops claiming it (e.g. on a mode switch).
extern bool modeOwnsFootswitchA;

void initSwitches();
void updateSwitchesMidi(); 
