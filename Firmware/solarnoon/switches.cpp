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

#include "switches.h"
#include "pin.h"
#include "midi.h"
#include "effects.h"
#include "display.h"
#include "settings.h"         // boardSettings.deviceMode, saveSettings()

static const int PINS[3] = { PIN_SW_A, PIN_SW_B, PIN_SW_C };

uint8_t switchCC[3]; // CC attribution lives in setup(), see switches.h
bool    modeOwnsFootswitchA = false; // see switches.h; reset every tick by updateModeIO()

void initSwitches() {
    pinMode(PIN_DISPLAY_SWITCH, INPUT_PULLUP);
    pinMode(PIN_EFFECT_SWITCH, INPUT_PULLUP);

    for (int i = 0; i < 3; i++)
        pinMode(PINS[i], INPUT_PULLUP);
}
//Switches Midi only concern the 3 switches A, B and C, not "effect" and "display" swicthes
void updateSwitchesMidi() {
    static bool          lastReading[3] = {HIGH, HIGH, HIGH};
    static bool          lastStable[3]  = {HIGH, HIGH, HIGH};
    static unsigned long lastChange[3]  = {0, 0, 0};
#ifdef SWITCH_COMBO_MODE_CHANGE
    // Set on the press that either fired the mode change (B/C) or got grabbed
    // as its modifier (A), cleared on the matching release: marks a press/
    // release pair that must stay silent (no MIDI, no per-mode action).
    static bool          comboConsumed[3] = {false, false, false};
#endif

    unsigned long now = millis();
    for (int i = 0; i < 3; i++) {
        bool reading = digitalRead(PINS[i]);
        if (reading != lastReading[i]) {
            lastChange[i]  = now;
            lastReading[i] = reading;
        }

        if (now - lastChange[i] >= 50 && reading != lastStable[i]) {
            lastStable[i] = reading;

#ifdef SWITCH_COMBO_MODE_CHANGE
            if (comboConsumed[i]) {
                if (reading == HIGH) comboConsumed[i] = false; // pair complete, stay silent
                continue;
            }
            // Fresh press of B/C while A is held: change mode, don't send MIDI,
            // and silence A's own release too (it was a modifier, not a tap).
            if (i != 0 && reading == LOW && lastStable[0] == LOW) {
                currentMode              = nextEnabledMode(currentMode, (i == 1) ? -1 : +1);
                boardSettings.deviceMode = (uint8_t)currentMode;
                saveSettings();
                drawModeScreen(currentMode);
                comboConsumed[i] = true;
                comboConsumed[0] = true;
                continue;
            }
#endif
            if (i == 0 && (currentMode == 1 || currentMode == 2)) {
                if (reading == LOW) { // press only
                    int effIdx = (currentMode == 2) ? mode2SelectedEffect : currentEffectIdx;
                    int effect = effectSections[effIdx];
                    toggleEffect(effect);
                    if (currentMode == 2) drawScreenMode2();
                    else                  drawScreen(currentScreen);
                }
            } else if (i == 0 && modeOwnsFootswitchA) {
                // Claimed by the active mode (see modeOwnsFootswitchA in
                // switches.h): it reads/debounces PIN_SW_A itself and decides
                // what a press does, so stay silent here.
            } else {
                sendMidiCC(switchCC[i], reading == LOW ? 127 : 0);
            }
        }
    }
}
