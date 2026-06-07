#include "three_way_switch.h"
#include "pin.h"
#include "leds_mono.h"
#include "effects.h"
#include "display.h"
#include "midi.h"             // sendMidiCC()
#include "Archetype-Henson.h" // OTHER, mode3SwitchOptions[] — mode 3

int threeWayPosition = 1; // start at middle

static void updatePositionLeds() {
    setIS31Led(LED_3WAY_UP,  threeWayPosition == 2 ? 255 : 0);
    setIS31Led(LED_3WAY_MID, threeWayPosition == 1 ? 255 : 0);
    setIS31Led(LED_3WAY_BOT, threeWayPosition == 0 ? 255 : 0);
}

void initThreeWaySwitch() {
    pinMode(PIN_3WAY_UP,   INPUT_PULLUP);
    pinMode(PIN_3WAY_DOWN, INPUT_PULLUP);
    updatePositionLeds();
}

void updateThreeWaySwitch() {
    if (currentMode == 2) {
        setIS31Led(LED_3WAY_UP,  0);
        setIS31Led(LED_3WAY_MID, 0);
        setIS31Led(LED_3WAY_BOT, 0);
        return;
    }

    if (currentMode != 3 && !isInInfoMode()) {
        int paramIdx = getSwitchParamIdx(effectSections[currentEffectIdx], currentScreen - 1);
        if (paramIdx < 0) {
            setIS31Led(LED_3WAY_UP,  0);
            setIS31Led(LED_3WAY_MID, 0);
            setIS31Led(LED_3WAY_BOT, 0);
            return;
        }
    }

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
        step = 2; // mode3SwitchOptions are plain 2-state switches — MID unused
    } else {
        int paramIdx = getSwitchParamIdx(effectSections[currentEffectIdx], currentScreen - 1);
        step = (isInInfoMode() || (paramIdx >= 0 && parameters[effectSections[currentEffectIdx]][paramIdx].type == THREE_WAY_SWITCH)) ? 1 : 2;
    }

    if (now - lastUpChange >= 50) {
        if (lastUpStable == HIGH && upReading == LOW && threeWayPosition > 0) {
            threeWayPosition = max(0, threeWayPosition - step);
            updatePositionLeds();
        }
        lastUpStable = upReading;
    }

    if (now - lastDownChange >= 50) {
        if (lastDownStable == HIGH && downReading == LOW && threeWayPosition < 2) {
            threeWayPosition = min(2, threeWayPosition + step);
            updatePositionLeds();
        }
        lastDownStable = downReading;
    }
}


// ── Switch updates ────────────────────────────────────────────────────────────

void updateSwitchMidi() {
    if ((isInInfoMode() && getInfoPage() != 1) || currentMode == 2 || currentMode == 3) return;
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
            setIS31Led(LED_3WAY_UP,  0);
            setIS31Led(LED_3WAY_MID, 0);
            setIS31Led(LED_3WAY_BOT, 0);
            lastPos = threeWayPosition;
            return;
        }

        // Sync switch position counter to stored parameter value; don't send MIDI.
        // SWITCH (2-state): map 0→pos 0 (BOT), 1→pos 2 (UP) — MID never lit.
        int pval = parameters[effect][paramIdx].value;
        threeWayPosition = (parameters[effect][paramIdx].type == THREE_WAY_SWITCH)
                           ? pval
                           : (pval ? 2 : 0);
        lastPos = threeWayPosition;
        updatePositionLeds();
        return;
    }

    // Physical switch moved: update parameter and send MIDI.
    lastPos = threeWayPosition;
    if (paramIdx < 0) return;

    Parameter& p = parameters[effect][paramIdx];
    int value;
    if (p.type == THREE_WAY_SWITCH) {
        value = threeWayPosition;
        p.value = value;
        p.known = true;
        sendMidiCC(p.midiNote, threeWayPosition * 50); //use to be 45, but 90 was not detected correctly
    } else {
        // Plain on/off switch driven from a 2-state position (UP/DOWN, MID
        // unused) — same ABSOLUTE-vs-TOGGLE plugin-mapping ambiguity as the
        // effect enable switches, so go through sendMidiSwitch() too.
        value = (threeWayPosition > 0) ? 1 : 0;
        p.value = value;
        p.known = true;
        sendMidiSwitch(p.midiNote, value);
    }
    notifyParamChanged(5);
    if (!isInInfoMode()) drawScreen(currentScreen);
}

// ── Mode 3 switch ─────────────────────────────────────────────────────────────
// The 3-way switch drives whichever of mode3SwitchOptions[] knob 3 currently
// has selected (mode3SelectedParam). All four options are plain on/off
// switches, so position BOT/MID = off, UP = on (MID is unreachable, step = 2).

void updateMode3Switch() {
    static int lastPos  = -1;
    static int lastSel  = -1;
    static int lastMode = -1;

    if (currentMode != 3 || isInInfoMode()) {
        lastMode = currentMode; // remember so re-entering mode 3 forces a resync
        return;
    }

    bool entered    = (lastMode != 3);
    bool selChanged = (mode3SelectedParam != lastSel);
    bool posMoved   = (threeWayPosition != lastPos);
    lastMode = currentMode;
    if (!entered && !selChanged && !posMoved) return;

    int paramIdx = mode3SwitchOptions[mode3SelectedParam];

    if (entered || selChanged) {
        lastSel = mode3SelectedParam;
        // Sync switch position to the selected param's stored value — just
        // re-displaying the existing state, so don't send MIDI.
        threeWayPosition = parameters[OTHER][paramIdx].value ? 2 : 0;
        lastPos = threeWayPosition;
        updatePositionLeds();
        return;
    }

    // Physical switch moved: update parameter and send MIDI — same
    // ABSOLUTE-vs-TOGGLE plugin-mapping ambiguity as any other 2-state
    // switch, so go through sendMidiSwitch().
    lastPos = threeWayPosition;
    int value = (threeWayPosition > 0) ? 1 : 0;
    parameters[OTHER][paramIdx].value = value;
    parameters[OTHER][paramIdx].known = true;
    sendMidiSwitch(parameters[OTHER][paramIdx].midiNote, value);
    drawScreenMode3();
}

