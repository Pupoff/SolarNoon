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

#include "three_way_switch.h"
#include "pin.h"
#include "leds_mono.h"
#include "effects.h"
#include "display.h"
#include "midi.h"             // sendMidiCC()

int threeWayPosition = 1; // start at middle

int readThreeWaySwitch() { return threeWayPosition; }

void setThreeWaySwitchLeds(int position) {
    setIS31Led(LED_3WAY_UP,  position == 2 ? 255 : 0);
    setIS31Led(LED_3WAY_MID, position == 1 ? 255 : 0);
    setIS31Led(LED_3WAY_BOT, position == 0 ? 255 : 0);
}

void drive3waySwitchToCC(uint8_t cc, int& lastSent) {
    static const uint8_t positionValues[3] = {0, 64, 127};
    int value = positionValues[readThreeWaySwitch()];
    if (value != lastSent) {
        lastSent = value;
        sendMidiCC(cc, value);
        displayRefreshNeeded = true;
    }
}

void initThreeWaySwitch() {
    pinMode(PIN_3WAY_UP,   INPUT_PULLUP);
    pinMode(PIN_3WAY_DOWN, INPUT_PULLUP);
    setThreeWaySwitchLeds(threeWayPosition);
}

void updateThreeWaySwitch() {
    // Modes 1 (parameter view), 3 (archetypeVSTglobalcontrols) and 4 (Solar
    // System) read the 3-way switch as a position selector and show that
    // position on its LEDs; mode 2's grid and any other placeholder slot just
    // turn the LEDs off, same treatment as info pages further down. Position
    // tracking itself (the debounce block below) always runs regardless of
    // mode, so readThreeWaySwitch() stays live everywhere — it must NOT be
    // gated behind the LED ownership check, or the position freezes solid
    // for any mode that isn't in the list.
    bool drivesLeds = (currentMode == 1 || currentMode == 3 || currentMode == 4);

    if (currentMode == 1 && !isInInfoMode()) {
        int paramIdx = getSwitchParamIdx(effectSections[currentEffectIdx], currentScreen - 1);
        if (paramIdx < 0) drivesLeds = false;
    }

    if (!drivesLeds) setThreeWaySwitchLeds(-1);

    static bool          lastUpReading   = HIGH;
    static bool          lastUpStable    = HIGH;
    static unsigned long lastUpChange    = 0;
    static bool          lastDownReading = HIGH;
    static bool          lastDownStable  = HIGH;
    static unsigned long lastDownChange  = 0;

    unsigned long now        = millis();
    bool          upReading  = digitalRead(PIN_3WAY_UP);
    bool          downReading= digitalRead(PIN_3WAY_DOWN);

    if (upReading != lastUpReading)     { lastUpChange   = now; lastUpReading   = upReading;   }
    if (downReading != lastDownReading) { lastDownChange = now; lastDownReading = downReading; }

    int step;
    if (currentMode == 3) {
        step = 2; // archetypeVSTglobalcontrols only has plain 2-state switches, MID unused
    } else if (currentMode == 1) {
        int paramIdx = getSwitchParamIdx(effectSections[currentEffectIdx], currentScreen - 1);
        step = (isInInfoMode() || (paramIdx >= 0 && effectControls[effectSections[currentEffectIdx]][paramIdx].type == THREE_WAY_SWITCH)) ? 1 : 2;
    } else {
        step = 1; // mode 4 and any other full 3-state reader: every position reachable
    }

    if (now - lastUpChange >= 50) {
        if (lastUpStable == HIGH && upReading == LOW && threeWayPosition > 0) {
            threeWayPosition = max(0, threeWayPosition - step);
            if (drivesLeds) setThreeWaySwitchLeds(threeWayPosition);
        }
        lastUpStable = upReading;
    }

    if (now - lastDownChange >= 50) {
        if (lastDownStable == HIGH && downReading == LOW && threeWayPosition < 2) {
            threeWayPosition = min(2, threeWayPosition + step);
            if (drivesLeds) setThreeWaySwitchLeds(threeWayPosition);
        }
        lastDownStable = downReading;
    }
}


// ── Switch updates ────────────────────────────────────────────────────────────

void updateSwitchMidi() {
    // Reads currentScreen/currentEffectIdx, mode-1 (parameter view) concepts only.
    if ((isInInfoMode() && getInfoPage() != 1) || currentMode != 1) return;
    static int lastPos    = -1;
    static int lastPage   = -1;
    static int lastEffect = -1;

    int page   = currentScreen - 1;
    int effect = effectSections[currentEffectIdx];

    bool pageChanged = (page != lastPage || effect != lastEffect);
    bool posMoved    = (threeWayPosition != lastPos);
    if (!pageChanged && !posMoved) return;

    int paramIdx = getSwitchParamIdx(effect, page);

    if (pageChanged) {
        lastPage   = page;
        lastEffect = effect;

        if (paramIdx < 0) {
            setThreeWaySwitchLeds(-1);
            lastPos = threeWayPosition;
            return;
        }

        // Sync switch position counter to stored parameter value; don't send MIDI.
        // SWITCH (2-state): map 0→pos 0 (BOT), 1→pos 2 (UP), MID never lit.
        int pval = effectControls[effect][paramIdx].value;
        threeWayPosition = (effectControls[effect][paramIdx].type == THREE_WAY_SWITCH)
                           ? pval
                           : (pval ? 2 : 0);
        lastPos = threeWayPosition;
        setThreeWaySwitchLeds(threeWayPosition);
        return;
    }

    // Physical switch moved: update parameter and send MIDI.
    lastPos = threeWayPosition;
    if (paramIdx < 0) return;

    Parameter& p = effectControls[effect][paramIdx];
    int value;
    if (p.type == THREE_WAY_SWITCH) {
        value = threeWayPosition;
        p.value = value;
        p.known = true;
        sendMidiCC(p.midiNote, threeWayPosition * 50); //use to be 45, but 90 was not detected correctly
    } else {
        // Plain on/off switch driven from a 2-state position (UP/DOWN, MID
        // unused), same ABSOLUTE-vs-TOGGLE plugin-mapping ambiguity as the
        // effect enable switches, so go through sendMidiSwitch() too.
        value = (threeWayPosition > 0) ? 1 : 0;
        p.value = value;
        p.known = true;
        sendMidiSwitch(p.midiNote, value);
    }
    notifyParamChanged(5);
    if (!isInInfoMode()) drawScreen(currentScreen);
}

