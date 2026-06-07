#include "knobs.h"
#include "debug.h"
#include "leds_mono.h"  // for ringIdx()
#include "pin.h"
#include "effects.h"
#include "display.h"
#include "midi.h"             // sendMidiCC()
#include "settings.h"         // boardSettings.enc3Sensitivity
#include "Archetype-Henson.h" // OTHER, OTHER_INPUT_GAIN, OTHER_OUTPUT_GAIN — mode 3

#ifdef KNOB3_IS_ENDLESS
#include <math.h>

// ── RV112FF endless dual-track potentiometer (atan2 method) ──────────────────
// The two linear wiper tracks create a rotating vector (fx, fy).
// atan2(fy, fx) converts it to a continuous angle; delta-accumulation tracks
// position across full rotations.
//
// ENC3_REVS_PER_RANGE : full turns required to sweep 0→100 % (lower = faster).
// ENC3_NOISE_FLOOR    : minimum dial steps (out of DIAL_RANGE) to register.
#define DIAL_RANGE       256
#define ENC3_NOISE_FLOOR   2
// Revolutions per full range is runtime: derived from boardSettings.enc3Sensitivity.
// sensitivity 100 → 1 rev (fastest), sensitivity 0 → 10 revs (slowest). Default 80 → ~2 revs.

static float enc3Accum    = 2047.5f;
static int   enc3PrevDial = -1;
static int   enc3LastDial = 0;   // raw 0..DIAL_RANGE-1, used by mode 2

void setKnob3EncoderPos(int v4095) {
    enc3Accum    = (float)constrain(v4095, 0, 4095);
    enc3PrevDial = -1; // force re-sync on next read
}

int getKnob3RawDial() { return enc3LastDial; }

static int readEncoder3() {
    long s1 = 0, s2 = 0;
    for (int i = 0; i < 8; i++) {
        s1 += analogRead(KNOB_PINS[2]); // track A
        s2 += analogRead(PIN_ENC3_B);   // track B
    }
    float fx = (float)(s1 / 8) / 2047.5f - 1.0f; // normalise to -1..+1
    float fy = (float)(s2 / 8) / 2047.5f - 1.0f;

    float angle   = atan2f(fy, fx);               // -π .. +π
    int   dialPos = (int)((angle + (float)M_PI) / ((float)M_PI * 2.0f) * DIAL_RANGE);
    dialPos = constrain(dialPos, 0, DIAL_RANGE - 1);

    enc3LastDial = dialPos; // always current, used by mode 2
    if (enc3PrevDial < 0) { enc3PrevDial = dialPos; return (int)enc3Accum; }

    // Shortest-path delta around the circle
    int delta = dialPos - enc3PrevDial;
    if (delta >  DIAL_RANGE / 2) delta -= DIAL_RANGE;
    if (delta < -DIAL_RANGE / 2) delta += DIAL_RANGE;
    enc3PrevDial = dialPos;

    if (abs(delta) < ENC3_NOISE_FLOOR) return (int)enc3Accum;

    int revsPerRange = constrain(1 + (100 - (int)boardSettings.enc3Sensitivity) / 11, 1, 10);
    enc3Accum = constrain(enc3Accum + (float)delta * 4095.0f / ((float)DIAL_RANGE * revsPerRange),
                          0.0f, 4095.0f);
    return (int)enc3Accum;
}

#else
void setKnob3EncoderPos(int) {}
int  getKnob3RawDial()       { return 0; }
#endif

void initKnobs() {
    analogReadResolution(12); // 12-bit ADC → 0–4095
    for (int i = 0; i < 5; i++)
        pinMode(KNOB_PINS[i], INPUT);
#ifdef KNOB3_IS_ENDLESS
    pinMode(PIN_ENC3_B, INPUT);
    analogSetPinAttenuation(PIN_ENC3_B, ADC_11db);
#endif
}

// 8× oversampling reduces ESP32 ADC noise from ±15 to ±5 counts.
// Hysteresis of 8 (> 5) blocks oscillation at step boundaries.
// A 0-100 step = 41 counts → 8-count dead zone is imperceptible.
#define KNOB_HYSTERESIS 40

int readKnob(int knobID) {
    int i = ringIdx(knobID);
    if (i < 0) {
        DBG_PRINTLN("readKnob: knobID out of range (1–5)");
        return 0;
    }
#ifdef KNOB3_IS_ENDLESS
    if (i == 2) return readEncoder3();
#endif
    long sum = 0;
    for (int s = 0; s < 8; s++) sum += analogRead(KNOB_PINS[i]);
    int raw = (int)(sum / 8);

    static int stable[5] = {-1,-1,-1,-1,-1};
    if      (stable[i] < 0)                        stable[i] = raw;
    else if (raw > stable[i] + KNOB_HYSTERESIS)    stable[i] = raw;
    else if (raw < stable[i] - KNOB_HYSTERESIS)    stable[i] = raw;
    return stable[i];
}

void debugPrintKnobs() {
    static unsigned long last = 0;
    if (millis() - last < 200) return; // throttle to 5 Hz
    last = millis();
    DBG_PRINTF("K1:%4d  K2:%4d  K3:%4d  K4:%4d  K5:%4d\n",
                  readKnob(1), readKnob(2), readKnob(3), readKnob(4), readKnob(5));
}

// ── Knob updates ──────────────────────────────────────────────────────────────

// A knob is "asleep" if it hasn't sent a value for KNOB_SLEEP_MS.
// While asleep, it ignores changes smaller than WAKE_THRESHOLD (0-100 scale)
// so that ADC noise can't cause spurious updates when the device is at rest.
// Once a deliberate movement of >= WAKE_THRESHOLD is detected, the knob wakes
// and responds normally (any change >= 1 sends).
#define KNOB_SLEEP_MS    3000
#define WAKE_THRESHOLD      2

void updateKnobsMidi() {
    if (isInInfoMode() && getInfoPage() != 1) return; // page 1 is read-only, knobs still control effect
    static unsigned long lastRun            = 0;
    static unsigned long lastTouched[5]     = {0, 0, 0, 0, 0};
    static bool          takenOver[5]       = {false, false, false, false, false};
    // Pickup-mode bookkeeping (no-ops in JUMP mode):
    //   pickedUp[k]         — true once the physical knob has "grabbed" the
    //                         parameter (soft takeover acquired).
    //   lastAppliedValue[k] — value we last wrote into parameters[][].value via
    //                         this knob; used to detect external (DAW feedback)
    //                         changes that should make the knob re-acquire.
    static bool          pickedUp[5]        = {false, false, false, false, false};
    static int           lastAppliedValue[5] = {-1, -1, -1, -1, -1};
    if (millis() - lastRun < 20) return;
    lastRun = millis();

    int  effect = effectSections[currentEffectIdx];
    int  page   = currentScreen - 1;
    bool redraw = false;

    for (int k = 0; k < 5; k++) {
        if (currentMode == 2) {
            ringMidiValues[k] = 0;
            ledRingBehavior(k + 1, SYNC_WITH_MIDI_CUSTOM);
            takenOver[k] = false;
            continue;
        }

        int targetEffect, paramIdx;
        if (currentMode == 3) {
            // Mode 3 ("OTHER" controls): only knobs 1 and 5 drive a parameter
            // (input/output gain). Knob 3 is the switch-selector, handled by
            // updateMode3Controls(); knobs 2 and 4 are unused.
            targetEffect = OTHER;
            paramIdx     = (k == 0) ? OTHER_INPUT_GAIN : (k == 4) ? OTHER_OUTPUT_GAIN : -1;
        } else {
            targetEffect = effect;
            paramIdx     = getKnobParamIdx(effect, page, k);
        }

        if (paramIdx < 0) {
            ringMidiValues[k] = 0;
            ledRingBehavior(k + 1, SYNC_WITH_MIDI_CUSTOM); // ring off
            takenOver[k] = false;
            continue;
        }

        int value  = map(readKnob(k + 1), 0, 4095, 0, 100);
        int target = parameters[targetEffect][paramIdx].value;

#define RING_VAL(v) (max(0, (v) - 5) * 255 / 100)

        if (lastKnobValues[k] == -1) {
#ifdef KNOB3_IS_ENDLESS
            if (k == 2) setKnob3EncoderPos(target * 4095 / 100);
#endif
            lastKnobValues[k]   = value;
            lastAppliedValue[k] = target;
#ifdef KNOB3_IS_ENDLESS
            takenOver[k] = (k == 2);
            // The endless encoder has no physical position to mismatch — its
            // accumulator was just seeded to `target` above, so it's already
            // aligned. Pots, however, must re-acquire on every page/effect entry.
            pickedUp[k]  = (k == 2) ? true : (KNOB_MODE == KNOB_MODE_JUMP);
#else
            takenOver[k] = false;
            pickedUp[k]  = (KNOB_MODE == KNOB_MODE_JUMP);
#endif
            ledRingBehavior(k + 1, SYNC_WITH_KNOB);
            continue;
        }

        // Ring always shows the current parameter value (handles both knob and MIDI changes).
        ledRingBehavior(k + 1, SYNC_WITH_KNOB);

        // Target moved without us (DAW feedback) — release pickup so the
        // physical knob has to re-meet the new value before driving it again.
        if (target != lastAppliedValue[k]) {
            lastAppliedValue[k] = target;
#ifdef KNOB3_IS_ENDLESS
            if (k != 2)
#endif
                pickedUp[k] = (KNOB_MODE == KNOB_MODE_JUMP);
        }

        int prevValue = lastKnobValues[k];
        int delta     = abs(value - prevValue);

        if (!pickedUp[k]) {
            // Soft takeover: ignore the knob until it physically crosses (or
            // lands on) the parameter's current value, then grab it from there.
            bool crossed = (prevValue - target) * (value - target) <= 0;
            lastKnobValues[k] = value;
            if (!crossed) continue;
            pickedUp[k] = true;
            // fall through and apply immediately — `value` is at/just past
            // `target` here, so there is no audible jump
        }

#ifdef KNOB3_IS_ENDLESS
        bool asleep = (k == 2) ? false : (millis() - lastTouched[k] >= KNOB_SLEEP_MS);
#else
        bool asleep = (millis() - lastTouched[k] >= KNOB_SLEEP_MS);
#endif
        if (asleep && delta < WAKE_THRESHOLD) continue;
        if (delta == 0) continue;

        if (!takenOver[k]) takenOver[k] = true;

        lastTouched[k]                           = millis();
        lastKnobValues[k]                        = value;
        lastAppliedValue[k]                      = value;
        parameters[targetEffect][paramIdx].value = value;
        parameters[targetEffect][paramIdx].known = true;
        sendMidiCC(parameters[targetEffect][paramIdx].midiNote, value * 127 / 100);
        if (currentMode != 3) notifyParamChanged(k); // mode 3 has its own screen, no row highlight
        redraw = true;
    }

    if (redraw && !isInInfoMode()) {
        if      (currentMode == 3) drawScreenMode3();
        else if (currentMode != 2) drawScreen(currentScreen);
    }
}
