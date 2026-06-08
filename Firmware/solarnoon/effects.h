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
#include "leds_rgb.h"  // for CRGB return type of getEffectColor()
#include "effectControls.h" // Section enum, EFFECT_COUNT, effectControls[][]

// Number of display-mode slots (1=parameter view, 2=effect grid, 3=OTHER
// controls, the rest free for new modes you write later). Bump this and add
// a case to drawModeScreen() (midi_a14h.ino) to add more.
#define MODE_COUNT 5

// Which mode slots are actually shown, modeEnabled[1..MODE_COUNT], index 0
// unused (modes are 1-based). Set explicitly in setup() (see midi_a14h.ino),
// long-pressing the effect switch skips disabled slots (nextEnabledMode());
// an enabled slot with no real screen falls back to a "not configured"
// placeholder (drawScreenModeGeneric(), defined in display.cpp and called
// from drawModeScreen() in midi_a14h.ino).
extern bool modeEnabled[MODE_COUNT + 1];

// Both filled once by initEffectControls() — see their definitions in
// effects.cpp for how they're derived straight from the Section enum and
// each section's own enable-switch label, instead of being named out by hand.
// EFFECT_COUNT itself lives in effectControls.h, right next to the Section
// enum it's derived from (= OTHER's enum value).
extern int         effectSections[]; // the cycle-able section IDs, in cycle order
extern const char* effectNames[];    // effectNames[k] == effectControls[effectSections[k]][0].label
extern int               currentEffectIdx;
extern int               currentMode;            // 1=white 2=orange 3=red
extern int               mode2SelectedEffect;    // 0-based effect index selected by knob 3
extern int               lastKnobValues[5];
extern CRGB              effectLedColor;         // color updateEffectLeds() last put on LED_EFFECT, reuse on other LEDs (e.g. LED_SWITCH_A) instead of recomputing

// Lets the active mode claim the effect switch's short-press action (long
// press always cycles modes, that stays generic — see handleEffectSwitch()).
// nullptr = generic behavior (modes 1/2 cycle/toggle the effect; anything else
// does nothing on short press, e.g. mode 4 "Solar System"); a self-contained
// mode points this at its own handler every tick it wants the press routed to
// it (e.g. archetypeVSTglobalcontrols points it at a captureless lambda that
// flips the OTHER section's on/off switch (effectControls[OTHER][4], label
// "on_off"), see case 3 in updateModeIO(), midi_a14h.ino).
// updateModeIO() resets it to nullptr before dispatching so a mode can't leave
// it claimed after switching away.
extern void (*modeEffectSwitchAction)();

// Page layout helpers, used by display.cpp too
int  getPageCount    (int effect);
int  getKnobParamIdx (int effect, int page, int slot); // 0-based page/slot; -1 = empty
int  getSwitchParamIdx(int effect, int page);           // -1 = no switch on this page

void initEffectControls();
void initMode();         // load currentMode from boardSettings
int  nextEnabledMode(int from, int step); // wrapping cycle through modeEnabled[] slots, step = +1/-1
CRGB getEffectColor();          // returns the active section's color as CRGB
CRGB getEffectColorByIdx(int idx); // same, for any 0-based effect index
void applyEffectColor(); // sets LED_EFFECT to the active section's color
void updateEffectLedColor();
void handleEffectSwitch();
void updateRingMode2();
// Henson groups stompboxes into sections toggled via the VST's tab icons
// (CAB for the amp, PRE-FX for boost/overdrive/compressor, POST-FX for
// chorus/reverb/delay), an effect that's "on" while its section is "off" is
// silently ignored by the plugin. Call this right after turning an effect ON
// to make sure its section is active too (turns it on + sends MIDI if not).
void ensureSectionActive(int effect);

// Display-only check: an effect reads as "on" only when its switch AND its
// section are both active (otherwise the plugin ignores it silently). Used by
// LED feedback and the mode-2 grid so the UI doesn't show a lie while an
// effect's .value is still "on" but its section just got switched off.
bool isEffectActiveForDisplay(int effect);

// Toggles an effect's on/off switch, the single source of truth for every
// "turn this effect on/off" button. Handles the case where the effect's
// switch is on but its section is off (see isEffectActiveForDisplay): wakes
// the section instead of toggling the effect off again.
void toggleEffect(int effect);
