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

#include "effects.h"
#include "display.h"          // currentScreen, drawScreen(), isInInfoMode()
#include "pin.h"              // PIN_EFFECT_SWITCH
#include "leds_rgb.h"         // setRGBled(), LED_EFFECT, FastLED safe here (no Control_Surface)
#include "leds_mono.h"        // forceRingRedraw()
#include "settings.h"         // boardSettings, saveSettings()
#include "midi.h"             // sendMidiCC()
#include "effectControls.h"

Parameter effectControls[SECTION_COUNT][MAX_PARAMS];
int       effectControlCount[SECTION_COUNT];

// EFFECT_COUNT lives in effectControls.h, right next to the Section enum it's
// derived from. effectSections[]/effectNames[] are filled from it once in
// initEffectControls() below (see there for how/why).
int         effectSections[EFFECT_COUNT];
const char* effectNames[EFFECT_COUNT];
int               currentEffectIdx    = 0;
int               currentMode         = 1;
int               mode2SelectedEffect = 0;
int               lastKnobValues[5] = { -1, -1, -1, -1, -1 };
CRGB              effectLedColor      = CRGB::Black;
void            (*modeEffectSwitchAction)() = nullptr; // see effects.h; reset every tick by updateModeIO()

// ── Page layout helpers ───────────────────────────────────────────────────────
// Index 0 of every section is the enable switch, always skipped here.
// KNOB/SLIDER params go to the 5 physical knobs; SWITCH/THREE_WAY go to the
// physical three-way switch (one per page).

int getKnobParamIdx(int effect, int page, int slot) {
    int list[MAX_PARAMS], count = 0;
    for (int i = 1; i <= effectControlCount[effect]; i++) {
        ParamType t = effectControls[effect][i].type;
        if (t == KNOB || t == SLIDER) list[count++] = i;
    }
    int idx = page * 5 + slot;
    return (idx < count) ? list[idx] : -1;
}

int getSwitchParamIdx(int effect, int page) {
    int list[MAX_PARAMS], count = 0;
    for (int i = 1; i <= effectControlCount[effect]; i++) {
        ParamType t = effectControls[effect][i].type;
        if (t == SWITCH || t == THREE_WAY_SWITCH) list[count++] = i;
    }
    return (page < count) ? list[page] : -1;
}

int getPageCount(int effect) {
    int knobs = 0, switches = 0;
    for (int i = 1; i <= effectControlCount[effect]; i++) {
        ParamType t = effectControls[effect][i].type;
        if (t == KNOB || t == SLIDER) knobs++;
        else                          switches++;
    }
    return max(1, max((knobs + 4) / 5, switches));
}

// ── Init ──────────────────────────────────────────────────────────────────────

void initEffectControls() {
    memset(effectControls,        0, sizeof(effectControls));
    memset(effectControlCount, 0, sizeof(effectControlCount));
    initializeEffectControlsTable();

    // effectSections[k] == k: the cycle-able sections occupy 0..EFFECT_COUNT-1
    // in their enum declaration order (see EFFECT_COUNT above). effectNames[k]
    // mirrors each section's own enable-switch label (index 0, "label =
    // section name" by convention — see initializeEffectControlsTable()), so
    // it can't drift out of sync with effectControls[][0].
    for (int k = 0; k < EFFECT_COUNT; k++) {
        effectSections[k] = k;
        effectNames[k]    = effectControls[k][0].label;
    }

    //Also initialise mode for boot. Not great here but sketchy in the setup directly...
    initMode();
}

// ── Effect LED feedback ───────────────────────────────────────────────────────

// LED_EFFECT shows the color of whichever effect is "current" for the active
// screen: the cycled-to effect in mode 1 (currentEffectIdx), or the
// knob-3-selected one in mode 2's overview grid (mode2SelectedEffect), its
// own selection, independent of (and not advanced by) currentEffectIdx.
static int currentDisplayedEffectIdx() {
    return (currentMode == 2) ? mode2SelectedEffect : currentEffectIdx;
}

void updateEffectLedColor() {
    int effIdx  = currentDisplayedEffectIdx();
    int effect  = effectSections[effIdx];
    int enabled = isEffectActiveForDisplay(effect);

    static int lastEffIdx  = -1;
    static int lastEnabled = -1;
    static int lastMode    = -1;
    if (effIdx == lastEffIdx && enabled == lastEnabled && currentMode == lastMode) return;
    lastEffIdx  = effIdx;
    lastEnabled = enabled;
    lastMode    = currentMode;

    CRGB col = getEffectColorByIdx(effIdx);
    if (!enabled) col.nscale8(20);
    effectLedColor = col; // cached for reuse, e.g. LED_SWITCH_A in loop()
    setRGBledRaw(LED_EFFECT, col);
}


// ── Mode ──────────────────────────────────────────────────────────────────────

// Storage for the mode-enable slots, no dynamic allocation. Values are set
// explicitly in setup() (see MODE_COUNT/modeEnabled in effects.h for how to
// configure which modes are shown).
bool modeEnabled[MODE_COUNT + 1];

// First enabled slot, used when the persisted mode is invalid/disabled.
// Falls back to 1 if somehow nothing is enabled (keeps the system from
// getting stuck with no usable mode).
static int firstEnabledMode() {
    for (int m = 1; m <= MODE_COUNT; m++)
        if (modeEnabled[m]) return m;
    return 1;
}

// Next enabled slot from `from`, `step` slots away (+1 = forward, -1 = backward),
// wrapping around, modes cycle continuously (unlike info pages there's no
// "exit" state to land on). Returns `from` itself if nothing else is enabled,
// so cycling can't get stuck looping.
int nextEnabledMode(int from, int step) {
    for (int i = 1; i <= MODE_COUNT; i++) {
        int m = (((from - 1 + i * step) % MODE_COUNT) + MODE_COUNT) % MODE_COUNT + 1;
        if (modeEnabled[m]) return m;
    }
    return from;
}

void initMode() {
    currentMode = boardSettings.deviceMode;
    if (currentMode < 1 || currentMode > MODE_COUNT || !modeEnabled[currentMode])
        currentMode = firstEnabledMode();
}

//LED Ring layout: here instead of leads_rgb.ccp because they're effect/mode-domain logic
// (selected effect index, section colors), I tried to keep leds_rgb.cpp as a
// generic hardware-driver file.

// Mode 1: ring oscillates ±10° (~7 hue units) around the effect color.
// Position spread of 20 units across 24 LEDs adds the subtle rainbow texture.
static void updateRingMode1() {
    static unsigned long lastTick = 0;
    if (millis() - lastTick < 40) return;
    lastTick = millis();
    uint8_t t      = (uint8_t)(millis() / 16);
    int8_t  hueOff = (int8_t)(((int)sin8(t) - 128) * 7 / 128);
    CHSV base = rgb2hsv_approximate(getEffectColor());
    for (int i = 0; i < RGB_RING_SIZE; i++) {
        uint8_t hue = base.hue + (uint8_t)hueOff + (uint8_t)(i * 20 / RGB_RING_SIZE);
        setRGBledRaw(RGB_RING_START + i, CRGB(CHSV(hue, base.sat, base.val)));
    }
}

// Mode 2: 24 LEDs split evenly among EFFECT_COUNT effects; selected segment brighter.
void updateRingMode2() {
    static unsigned long lastTick = 0;
    if (millis() - lastTick < 40) return;
    lastTick = millis();
    for (int i = 0; i < RGB_RING_SIZE; i++) {
        int k = ((RGB_RING_SIZE - 1 - i) * EFFECT_COUNT) / RGB_RING_SIZE;
        RGBColor c = sectionColors[effectSections[k]];
        CRGB color(c.r, c.g, c.b);
        if (k != mode2SelectedEffect) color.nscale8(5);
        setRGBledRaw(RGB_RING_START + i, color);
    }
}


// ── Effect switch ─────────────────────────────────────────────────────────────

CRGB getEffectColor() {
    RGBColor c = sectionColors[effectSections[currentEffectIdx]];
    return CRGB(c.r, c.g, c.b);
}

CRGB getEffectColorByIdx(int idx) {
    RGBColor c = sectionColors[effectSections[idx]];
    return CRGB(c.r, c.g, c.b);
}

// Lights LED_EFFECT with the current section's color.
void applyEffectColor() {
    setRGBled(LED_EFFECT, getEffectColor());
}

// Maps an effect section to the OTHER-section "tab" switch that must be on
// for it to actually work in the plugin. AMP -> cab, boost/overdrive/compressor
// -> pre-FX, chorus/reverb/delay -> post-FX.
static int sectionSwitchFor(int effect) {
    switch (effect) {
        case AMP:                                    return 7; // CAB_SECTION
        case BOOST: case OVERDRIVE: case COMPRESSOR: return 5; // PRE_FX_SECTION
        case DELAY: case REVERB:    case CHORUS:     return 6; // POST_FX_SECTION
        default:                                     return -1;
    }
}

void ensureSectionActive(int effect) {
    int sec = sectionSwitchFor(effect);
    if (sec < 0) return;
    Parameter& p = effectControls[OTHER][sec];
    if (p.value) return; // section already active

    p.value = 1;
    p.known = true;
    // Section switches (PRE/POST_FX, CAB) are also reachable directly via the
    // 3-way switch in the "archetypeVSTglobalcontrols" mode (midi_a14h.ino),
    // which always sends them as absolute values, sendMidiSwitch()/SWITCH_MIDI_MODE only governs
    // momentary stompbox-style toggles (footswitches, effect switch). Sending
    // the same CC two different ways depending on the trigger would confuse
    // the plugin's MIDI Learn mapping, so mirror the 3-way switch's convention here.
    sendMidiCC(p.midiNote, p.value ? 100 : 0);

    // The section was off, so any other effect in it that the preset left
    // "on" was actually inactive and invisible. Turning the section on would
    // suddenly bring those back to life too, turn them off so only the
    // effect the user just enabled ends up running.
    for (int i = 0; i < EFFECT_COUNT; i++) {
        int other = effectSections[i];
        if (other == effect || sectionSwitchFor(other) != sec) continue;
        Parameter& op = effectControls[other][0];
        if (!op.value) continue;
        op.value = 0;
        op.known = true;
        sendMidiSwitch(op.midiNote, op.value);
    }
}

// Whether an effect should be *displayed* as on (LEDs, mode-2 grid): its own
// switch must be on AND its section must be active, the plugin silently
// ignores an effect whose section is off, so showing it as "on" would be a
// lie. We deliberately don't touch .value here: ensureSectionActive() already
// turns the other effects in a section off the moment that section wakes up,
// so this is purely cosmetic for the (temporary) mismatched state in between.
bool isEffectActiveForDisplay(int effect) {
    if (!effectControls[effect][0].value) return false;
    int sec = sectionSwitchFor(effect);
    if (sec < 0) return true;
    return effectControls[OTHER][sec].value != 0;
}

// Toggles an effect's enable switch, the action behind every "turn this
// effect on/off" button (footswitch A, effect-switch in mode 2, ...).
//
// Special case: the effect's own switch can already be "on" while its section
// is off (isEffectActiveForDisplay() shows it as off in that case, see
// above). Naively toggling .value here would actually turn the effect OFF
// from the plugin's perspective, the opposite of what pressing "on" means to
// the user, who only sees a disabled-looking effect and wants it to play.
// So: if that's the situation, just wake the section instead of touching the
// effect's own switch, ensureSectionActive() already disables any siblings.
void toggleEffect(int effect) {
    if (effectControls[effect][0].value && !isEffectActiveForDisplay(effect)) {
        ensureSectionActive(effect);
        return;
    }

    effectControls[effect][0].value ^= 1;
    effectControls[effect][0].known  = true;
    sendMidiSwitch(effectControls[effect][0].midiNote, effectControls[effect][0].value);
    if (effectControls[effect][0].value) ensureSectionActive(effect);
}

#define LONG_PRESS_MS 600

void handleEffectSwitch() {
    static bool          lastReading    = HIGH;
    static bool          lastStable     = HIGH;
    static unsigned long lastChange     = 0;
    static unsigned long pressedAt      = 0;
    static bool          longPressFired = false;

    bool reading = digitalRead(PIN_EFFECT_SWITCH);
    if (reading != lastReading) { lastChange = millis(); lastReading = reading; }
    if ((millis() - lastChange) <= 50) return; // still bouncing

    if (lastStable == HIGH && reading == LOW) {
        // confirmed press down
        pressedAt      = millis();
        longPressFired = false;
    }

    if (reading == LOW && !longPressFired && (millis() - pressedAt) >= LONG_PRESS_MS) {
        longPressFired           = true;
        currentMode              = nextEnabledMode(currentMode, +1);
        boardSettings.deviceMode = (uint8_t)currentMode;
        saveSettings();
        drawModeScreen(currentMode);
    }

    if (lastStable == LOW && reading == HIGH && !longPressFired && !isInInfoMode()) {
        if (currentMode == 2) {
            // toggle enable of knob-3-selected effect
            toggleEffect(effectSections[mode2SelectedEffect]);
            drawScreenMode2();
        } else if (currentMode == 1) {
            // cycle effect
            currentEffectIdx = (currentEffectIdx + 1) % EFFECT_COUNT;
            currentScreen    = 1;
            for (int k = 0; k < 5; k++) lastKnobValues[k] = -1;
            forceRingRedraw();
            applyEffectColor();
            drawScreen(1);
        } else if (modeEffectSwitchAction) {
            // Claimed by the active mode (see modeEffectSwitchAction in
            // effects.h): it decides what a short press does, so just route
            // the press to it.
            modeEffectSwitchAction();
        }
    }

    lastStable = reading;
}
