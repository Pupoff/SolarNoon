#include "effects.h"
#include "display.h"          // currentScreen, drawScreen(), isInInfoMode()
#include "pin.h"              // PIN_EFFECT_SWITCH
#include "leds_rgb.h"         // setRGBled(), LED_EFFECT — FastLED safe here (no Control_Surface)
#include "leds_mono.h"        // forceRingRedraw()
#include "settings.h"         // boardSettings, saveSettings()
#include "midi.h"             // sendMidiCC()
#include "Archetype-Henson.h"

Parameter parameters[SECTION_COUNT][MAX_PARAMS];
int       parameters_number[SECTION_COUNT];

const int         effectSections[] = { AMP, DELAY, COMPRESSOR, OVERDRIVE, BOOST, REVERB, CHORUS };
const char* const effectNames[]    = { "AMP", "DELAY", "COMP.", "OVERDRIVE", "BOOST", "REVERB", "CHORUS" };
const int         EFFECT_COUNT     = 7;
int               currentEffectIdx    = 0;
int               currentMode         = 1;
int               mode2SelectedEffect = 0;
int               mode3SelectedParam  = MODE3_DEFAULT_SWITCH_SEL; // OTHER_DOUBLER by default
int               lastKnobValues[5] = { -1, -1, -1, -1, -1 };

// ── Page layout helpers ───────────────────────────────────────────────────────
// Index 0 of every section is the enable switch — always skipped here.
// KNOB/SLIDER params go to the 5 physical knobs; SWITCH/THREE_WAY go to the
// physical three-way switch (one per page).

int getKnobParamIdx(int effect, int page, int slot) {
    int list[MAX_PARAMS], count = 0;
    for (int i = 1; i <= parameters_number[effect]; i++) {
        ParamType t = parameters[effect][i].type;
        if (t == KNOB || t == SLIDER) list[count++] = i;
    }
    int idx = page * 5 + slot;
    return (idx < count) ? list[idx] : -1;
}

int getSwitchParamIdx(int effect, int page) {
    int list[MAX_PARAMS], count = 0;
    for (int i = 1; i <= parameters_number[effect]; i++) {
        ParamType t = parameters[effect][i].type;
        if (t == SWITCH || t == THREE_WAY_SWITCH) list[count++] = i;
    }
    return (page < count) ? list[page] : -1;
}

int getPageCount(int effect) {
    int knobs = 0, switches = 0;
    for (int i = 1; i <= parameters_number[effect]; i++) {
        ParamType t = parameters[effect][i].type;
        if (t == KNOB || t == SLIDER) knobs++;
        else                          switches++;
    }
    return max(1, max((knobs + 4) / 5, switches));
}

// ── Init ──────────────────────────────────────────────────────────────────────

void initParameters() {
    memset(parameters,        0, sizeof(parameters));
    memset(parameters_number, 0, sizeof(parameters_number));
    initializeParameters();
}

// ── Effect LED feedback ───────────────────────────────────────────────────────
//   SW_C → effect color (dimmed when section disabled)
//   SW_A, SW_B → white (always)
// No-op when nothing changed — safe to call every loop().

void updateEffectLeds() {
    int effect  = effectSections[currentEffectIdx];
    int enabled = isEffectActiveForDisplay(effect);

    static int lastEffect  = -1;
    static int lastEnabled = -1;
    static int lastMode    = -1;
    if (effect == lastEffect && enabled == lastEnabled && currentMode == lastMode) return;
    lastEffect  = effect;
    lastEnabled = enabled;
    lastMode    = currentMode;

    CRGB col = getEffectColor();
    if (!enabled) col.nscale8(20);
    setRGBledRaw(LED_EFFECT, col);
}




// ── Mode ──────────────────────────────────────────────────────────────────────

CRGB getModeColor() {
    switch (currentMode) {
        case 2:  return CRGB(255,  80,   0); // orange
        case 3:  return CRGB(255,   0,   0); // red
        default: return CRGB(255, 255, 255); // white (mode 1)
    }
}

void initMode() {
    currentMode = boardSettings.deviceMode;
    if (currentMode < 1 || currentMode > 3) currentMode = 1;
}

void updateModeLed() {
    static int lastMode = -1;
    if (currentMode == lastMode) return;
    lastMode = currentMode;
    setRGBledRaw(LED_MODE, getModeColor());
}

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
static void updateRingMode2() {
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

// Mode 3: 24 LEDs split evenly among the 4 OTHER-switch options (same layout
// idea as mode 2); selected segment brighter. Unlike mode 2, all segments
// share one warm-white color — these are positions of one selector, not
// distinct effects with their own identity color.
static void updateRingMode3() {
    static unsigned long lastTick = 0;
    if (millis() - lastTick < 40) return;
    lastTick = millis();
    const int  optionCount = sizeof(mode3SwitchOptions) / sizeof(mode3SwitchOptions[0]);
    const CRGB warmWhite(255, 180, 110);
    for (int i = 0; i < RGB_RING_SIZE; i++) {
        int  k     = ((RGB_RING_SIZE - 1 - i) * optionCount) / RGB_RING_SIZE;
        CRGB color = warmWhite;
        if (k != mode3SelectedParam) color.nscale8(5);
        setRGBledRaw(RGB_RING_START + i, color);
    }
}

void updateRingByMode() {
    if      (currentMode == 1)     {
    //updateRingMode1(); //Uncomment for RGB ring
    for (int i = 0; i < RGB_RING_SIZE; i++)
            setRGBledRaw(RGB_RING_START + i, CRGB::Black);
    }
    else if (currentMode == 2) updateRingMode2();
    else if (currentMode == 3) updateRingMode3();
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
        case AMP:                                    return CAB_SECTION;
        case BOOST: case OVERDRIVE: case COMPRESSOR: return PRE_FX_SECTION;
        case DELAY: case REVERB:    case CHORUS:     return POST_FX_SECTION;
        default:                                     return -1;
    }
}

void ensureSectionActive(int effect) {
    int sec = sectionSwitchFor(effect);
    if (sec < 0) return;
    Parameter& p = parameters[OTHER][sec];
    if (p.value) return; // section already active

    p.value = 1;
    p.known = true;
    // Section switches (PRE/POST_FX, CAB) are also reachable directly via the
    // 3-way switch in mode 3 (updateMode3Switch), which always sends them as
    // absolute values — sendMidiSwitch()/SWITCH_MIDI_MODE only governs
    // momentary stompbox-style toggles (footswitches, effect switch). Sending
    // the same CC two different ways depending on the trigger would confuse
    // the plugin's MIDI Learn mapping, so mirror the 3-way switch's convention here.
    sendMidiCC(p.midiNote, p.value ? 100 : 0);

    // The section was off, so any other effect in it that the preset left
    // "on" was actually inactive and invisible. Turning the section on would
    // suddenly bring those back to life too — turn them off so only the
    // effect the user just enabled ends up running.
    for (int i = 0; i < EFFECT_COUNT; i++) {
        int other = effectSections[i];
        if (other == effect || sectionSwitchFor(other) != sec) continue;
        Parameter& op = parameters[other][0];
        if (!op.value) continue;
        op.value = 0;
        op.known = true;
        sendMidiSwitch(op.midiNote, op.value);
    }
}

// Whether an effect should be *displayed* as on (LEDs, mode-2 grid): its own
// switch must be on AND its section must be active — the plugin silently
// ignores an effect whose section is off, so showing it as "on" would be a
// lie. We deliberately don't touch .value here: ensureSectionActive() already
// turns the other effects in a section off the moment that section wakes up,
// so this is purely cosmetic for the (temporary) mismatched state in between.
bool isEffectActiveForDisplay(int effect) {
    if (!parameters[effect][0].value) return false;
    int sec = sectionSwitchFor(effect);
    if (sec < 0) return true;
    return parameters[OTHER][sec].value != 0;
}

// Toggles an effect's enable switch — the action behind every "turn this
// effect on/off" button (footswitch A, effect-switch in mode 2, ...).
//
// Special case: the effect's own switch can already be "on" while its section
// is off (isEffectActiveForDisplay() shows it as off in that case — see
// above). Naively toggling .value here would actually turn the effect OFF
// from the plugin's perspective — the opposite of what pressing "on" means to
// the user, who only sees a disabled-looking effect and wants it to play.
// So: if that's the situation, just wake the section instead of touching the
// effect's own switch — ensureSectionActive() already disables any siblings.
void toggleEffect(int effect) {
    if (parameters[effect][0].value && !isEffectActiveForDisplay(effect)) {
        ensureSectionActive(effect);
        return;
    }

    parameters[effect][0].value ^= 1;
    parameters[effect][0].known  = true;
    sendMidiSwitch(parameters[effect][0].midiNote, parameters[effect][0].value);
    if (parameters[effect][0].value) ensureSectionActive(effect);
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
        currentMode              = (currentMode % 3) + 1;
        boardSettings.deviceMode = (uint8_t)currentMode;
        saveSettings();
        setRGBledRaw(LED_MODE, getModeColor());
        if      (currentMode == 2) drawScreenMode2();
        else if (currentMode == 3) drawScreenMode3();
        else                       drawScreen(currentScreen);
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
        } else if (currentMode == 3) {
            // toggle OTHER_ON_OFF — same control as footswitch A in this mode
            Parameter& p = parameters[OTHER][OTHER_ON_OFF];
            p.value ^= 1;
            p.known  = true;
            sendMidiSwitch(p.midiNote, p.value);
            drawScreenMode3();
        }
    }

    lastStable = reading;
}
