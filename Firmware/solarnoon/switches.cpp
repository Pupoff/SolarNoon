#include "switches.h"
#include "pin.h"
#include "midi.h"
#include "effects.h"
#include "display.h"
#include "Archetype-Henson.h" // OTHER, OTHER_ON_OFF — mode 3 footswitch A mapping

static const int     PINS[3] = { PIN_SW_A,  PIN_SW_B,  PIN_SW_C };
static const uint8_t CCS[3]  = { CC_SW_A,   CC_SW_B,   CC_SW_C  };

void initSwitches() {
    for (int i = 0; i < 3; i++)
        pinMode(PINS[i], INPUT_PULLUP);
}

void updateSwitchesMidi() {
    static bool          lastReading[3] = {HIGH, HIGH, HIGH};
    static bool          lastStable[3]  = {HIGH, HIGH, HIGH};
    static unsigned long lastChange[3]  = {0, 0, 0};

    unsigned long now = millis();
    for (int i = 0; i < 3; i++) {
        bool reading = digitalRead(PINS[i]);
        if (reading != lastReading[i]) {
            lastChange[i]  = now;
            lastReading[i] = reading;
        }
        
        if (now - lastChange[i] >= 50 && reading != lastStable[i]) {
            lastStable[i] = reading;
            if (i == 0 && (currentMode == 1 || currentMode == 2)) {
                if (reading == LOW) { // press only
                    int effIdx = (currentMode == 2) ? mode2SelectedEffect : currentEffectIdx;
                    int effect = effectSections[effIdx];
                    toggleEffect(effect);
                    if (currentMode == 2) drawScreenMode2();
                    else                  drawScreen(currentScreen);
                }
            } else if (i == 0 && currentMode == 3) {
                if (reading == LOW) { // press only — toggle OTHER_ON_OFF
                    Parameter& p = parameters[OTHER][OTHER_ON_OFF];
                    p.value ^= 1;
                    p.known  = true;
                    sendMidiSwitch(p.midiNote, p.value);
                    drawScreenMode3();
                }
            } else {
                sendMidiCC(CCS[i], reading == LOW ? 127 : 0);
            }
        }
    }
}
