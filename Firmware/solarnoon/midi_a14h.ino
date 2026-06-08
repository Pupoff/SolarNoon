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

#include "pin.h"
#include "leds_mono.h"
#include "leds_rgb.h"
#include "display.h"
#include "knobs.h"
#include "midi.h"
#include "effects.h"
#include "effectControls.h" // OTHER, effectControls[OTHER][2] ("doubler"), UserLed example in loop()
#include "three_way_switch.h"
#include "switches.h"
#include "startup.h"
#include "debug.h"
#include "settings.h"
#include "battery.h"
#include "direct_controls.h" // ButtonEdge/buttonPressed, driveKnobFromControl & friends


void setup() {
    Serial.begin(115200);

    //Load parameters and init midi
    loadSettings(); // must be before initDisplay() so contrast is applied at boot
    initMidi();

    // Which display-mode slots are active, see README.md "Display modes" for
    // how to add your own. Must be set before initEffectControls() (it calls
    // initMode(), which validates the persisted mode against modeEnabled[]).
    modeEnabled[1] = true;  // parameter view
    modeEnabled[2] = true;  // effect grid    
    modeEnabled[3] = true;  // OTHER controls  
    modeEnabled[4] = true; // free slot
    modeEnabled[5] = true; // free slot

    initEffectControls(); // must run before initDisplay() so screen 1 has valid data
    buildMidiLookup(); // must run after initEffectControls() so midiNote fields are populated

    //Display
    initDisplay();
    //LED
    initLedsMonochrome();
    initRGBLeds();
    initStatusLeds();
    //UI 
    initKnobs();
    initThreeWaySwitch();
    initSwitches();
    initBattery();
    startupAnimation();


    displayRefreshNeeded = true;

    // Default CC each footswitch sends, see README.md "Footswitches".
    switchCC[0] = CC_SW_A;
    switchCC[1] = CC_SW_B;
    switchCC[2] = CC_SW_C;

    // One-shot LED-to-MIDI bindings go here, see README.md "mapLEDtoMIDI".
    // Examples (commented out):
    //mapLEDtoMIDI(StatusLed(LED_WIFI), {1, 70});
    //mapLEDtoMIDI(RGBLed(LED_SWITCH_A), {1, 71}, {255, 0, 0});
    //mapLEDtoMIDI(RingLED(3), {1, 74});
}


// Tutorial mode: a worked example of adding a brand new display mode end to
// end (screen + LEDs + rings + footswitch + MIDI). Walked through step by
// step in README.md, "Solar System walkthrough" — read that first if you're
// using this as a template for your own mode.
void drawScreenMode4() {
    display.clearDisplay();

    // ── Knockout header ───────────────────────────────────────────────────────
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("SOLAR SYSTEM");
    display.setTextColor(SH110X_WHITE);

    // ── Orbits ────────────────────────────────────────────────────────────────
    // One concentric ring per knob, viewed from above: knob position places
    // its "planet" dot around its own ring, knob 1 = innermost.
    const int cx = 64, cy = 37;
    display.fillCircle(cx, cy, 2, SH110X_WHITE); // the "sun"

    // A small star that tracks the 3-way switch's position (0=up, 1=mid,
    // 2=bottom): three fixed spots along the right edge, read straight off
    // readThreeWaySwitch() — folds a third physical control into a screen
    // that would otherwise only react to the 5 knobs.
    {
        static const int starY[3] = {59, 37, 13}; // up, mid, bottom
        int sx = 116, sy = starY[readThreeWaySwitch()];
        display.drawLine(sx - 3, sy,     sx + 3, sy,     SH110X_WHITE);
        display.drawLine(sx,     sy - 3, sx,     sy + 3, SH110X_WHITE);
    }

    for (int k = 0; k < 5; k++) {
        int radius = 5 + k * 5; // 5,10,15,20,25: fits between the header and the bottom edge
        display.drawCircle(cx, cy, radius, SH110X_WHITE);

        int   value = map(readKnob(k + 1), 0, 4095, 0, 100);
        // +PI/2: with y growing downward, angle 0 sits at 3 o'clock and
        // increasing angle already sweeps clockwise, so adding a quarter turn
        // just shifts the whole picture's "zero" from 3 o'clock to 6 o'clock,
        // i.e. rotates everything 90° clockwise.
        float angle = (value / 100.0f) * 2.0f * PI + (float)PI / 2.0f;
        int   px    = cx + (int)(radius * cosf(angle));
        int   py    = cy + (int)(radius * sinf(angle));
        display.fillCircle(px, py, 2, SH110X_WHITE);
    }

    display.display();
}


// "archetypeVSTglobalcontrols" (mode 3): a self-contained mode, same pattern as
// "Solar System" (mode 4, see GETTING_STARTED.md) — it owns its screen, ring,
// LEDs, footswitch, knobs and 3-way switch outright instead of plugging into
// the shared parameter/pickup machinery. It exposes a few of the OTHER
// section's controls (input/output gain, on/off, and a knob-3-selectable group
// of on/off switches mapped onto the physical 3-way switch).
//
// These 4 on/off switches share the 3-way switch; knob 3 picks which one is
// currently mapped onto it (its index here is the knob-3 selection, 0-based).
static const int vstSwitchOptions[4] = { 5, 6, 7, 2 }; // PRE_FX_SECTION, POST_FX_SECTION, CAB_SECTION, OTHER_DOUBLER
static int       vstSelectedOption   = 3; // OTHER_DOUBLER by default

void drawScreenArchetypeVSTglobalcontrols() {
    display.clearDisplay();

    // ── Knockout header ───────────────────────────────────────────────────────
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("OTHER");
    display.setTextColor(SH110X_WHITE);

    // Numerals mirror mode 1's convention: "I"/"V" for the knobs that drive
    // these params (knob 1 → input gain, knob 5 → output gain, see case 3
    // below), ">" for the on/off switches (same as the parameter-view's
    // switch rows).
    char buf[8];
    const Parameter& inGain = effectControls[OTHER][0]; // OTHER_INPUT_GAIN
    snprintf(buf, sizeof(buf), "%d", inGain.value);
    drawParamRow(11, "I", inGain.label, inGain.known ? buf : "?");

    char buf2[8];
    const Parameter& outGain = effectControls[OTHER][1]; // OTHER_OUTPUT_GAIN
    snprintf(buf2, sizeof(buf2), "%d", outGain.value);
    drawParamRow(20, "V", outGain.label, outGain.known ? buf2 : "?");

    const Parameter& onOff = effectControls[OTHER][4]; // OTHER_ON_OFF
    drawParamRow(38, ">", onOff.label, !onOff.known ? "?" : (onOff.value ? "ON" : "OFF"));

    const Parameter& sel = effectControls[OTHER][vstSwitchOptions[vstSelectedOption]];
    drawParamRow(56, ">", sel.label, !sel.known ? "?" : (sel.value ? "ON" : "OFF"));

    display.display();
}

// Single place that knows how to draw any display mode (real or placeholder).
// modeEnabled[]/nextEnabledMode() in effects.cpp decide WHICH mode is current
// (see modeEnabled[] in setup() to add/remove a mode); this just draws
// whatever that turns out to be. Called from both handleDisplay() below and
// handleEffectSwitch() (effects.cpp, when long-pressing cycles the mode).
void drawModeScreen(int mode) {
    switch (mode) {
        case 1: drawScreen(currentScreen); break;
        case 2: drawScreenMode2();         break;
        case 3: drawScreenArchetypeVSTglobalcontrols(); break;
        case 4: drawScreenMode4();         break;
        // Add a case here when you write a new drawScreenModeN(), then flip
        // its modeEnabled[] slot to true in setup().
        default: drawScreenModeGeneric(mode); break; // slot enabled but no real screen written yet
    }
}

// Per-mode "ambient" behavior: which LEDs/rings light up and which CC each
// footswitch sends while this mode is active. Unlike drawModeScreen() (a
// one-shot redraw on displayRefreshNeeded), this runs every loop() tick so
// LEDs/rings/MIDI stay live, e.g. tracking knob movement, regardless of
// whether the screen itself needs to change. Companion to drawModeScreen():
// that one decides what the SCREEN shows for a mode, this one decides what
// the LEDs/switches DO, both keyed on the same mode number.
void updateModeIO(int mode) {
    // Generic "did we just switch onto this page" signal: updateModeIO() only
    // runs the matching case, so a case's own statics can't tell entry from
    // continuation by themselves (unlike updateThreeWaySwitch() etc., which run
    // every tick regardless of mode and can watch currentMode change directly).
    static int lastActiveMode = -1;
    bool        modeJustEntered = (mode != lastActiveMode);
    lastActiveMode = mode;

    // Default: footswitch A behaves generically (switchCC[0] pulse, see
    // switches.h), and the effect switch's short press does whatever modes 1/2
    // hardcode (see effects.h). A self-contained mode that wants either of
    // these re-claims them every tick in its own case below — resetting them
    // here means ownership auto-releases the instant that mode stops being
    // current.
    modeOwnsFootswitchA    = false;
    modeEffectSwitchAction = nullptr;

    switch (mode) {
        case 1:
            setRGBledRaw(LED_MODE, CRGB::White);
            setRGBledRaw(LED_SWITCH_A, effectLedColor);
            setRGBledRaw(LED_SWITCH_B, CRGB(160, 160, 160));
            setRGBledRaw(LED_SWITCH_C, CRGB(160, 160, 160));
            for (int i = 0; i < RGB_RING_SIZE; i++)
                setRGBledRaw(RGB_RING_START + i, CRGB::Black);
            // Example of a per-page LED rebind
            {
                bool doublerOn = effectControls[OTHER][2].value; // OTHER_DOUBLER
                setStatusLed(LED_USER1, doublerOn ? 255 : 0);
                setStatusLed(LED_USER2, doublerOn ? 255 : 0);
            }
            break;
        case 2:
            setRGBledRaw(LED_MODE, CRGB::White);
            setRGBledRaw(LED_SWITCH_A, effectLedColor);
            setRGBledRaw(LED_SWITCH_B, CRGB(160, 160, 160));
            setRGBledRaw(LED_SWITCH_C, CRGB(160, 160, 160));
            updateMode2Controls();
            updateRingMode2();
            // Example of a per-page LED rebind
            setStatusLed(LED_USER1, (mode2SelectedEffect % 2) == 0 ? 255 : 0);
            setStatusLed(LED_USER2, (mode2SelectedEffect % 2) == 0 ? 0    : 255);
            break;
        case 3: {
            // "archetypeVSTglobalcontrols": fully self-contained (screen, ring,
            // LEDs, knobs, footswitch, 3-way switch all owned right here), see
            // GETTING_STARTED.md — same pattern mode 4 ("Solar System") follows.
            static int lastSel = -1;

            setRGBledRaw(LED_MODE,     CRGB::Black);
            setRGBledRaw(LED_EFFECT,   CRGB::Black);
            setRGBledRaw(LED_SWITCH_A, CRGB::White);
            setRGBledRaw(LED_SWITCH_B, CRGB::White);
            setRGBledRaw(LED_SWITCH_C, CRGB::White);
            setStatusLed(LED_USER1, 0);
            setStatusLed(LED_USER2, 0);

            // Footswitch A: claim it from the generic dispatcher (see
            // modeOwnsFootswitchA, switches.h) and drive the OTHER section's
            // on/off switch directly from it via driveSwitchFromControl()
            // (debounce + toggle, one call).
            modeOwnsFootswitchA = true;
            static ButtonEdge swA;
            driveSwitchFromControl(PIN_SW_A, swA, effectControls[OTHER][4]); // OTHER_ON_OFF

            // Effect switch: claim its short-press action too (see
            // modeEffectSwitchAction, effects.h) — same control, same toggle.
            // Captureless lambda: effectControls/OTHER are all globals/enum
            // constants, nothing to capture, so it converts straight to the
            // void(*)() modeEffectSwitchAction expects.
            modeEffectSwitchAction = []() { driveToggleFromControl(effectControls[OTHER][4]); }; // OTHER_ON_OFF

            // Knob 1 → input gain, knob 5 → output gain: fully self-contained,
            // direct mapping (like mode 4's planets), no soft takeover — first
            // touch can cause an audible jump, see GETTING_STARTED.md "KNOB_MODE".
            driveKnobFromControl(1, effectControls[OTHER][0]); // OTHER_INPUT_GAIN
            driveKnobFromControl(5, effectControls[OTHER][1]); // OTHER_OUTPUT_GAIN
            ringMidiValues[1] = 0;
            ringMidiValues[2] = 0; // knob 3 is a selector, not a parameter: ring stays off
            ringMidiValues[3] = 0;

            // Knob 3 picks which of the 4 on/off options the 3-way switch drives.
#ifdef KNOB3_IS_ENDLESS
            readKnob(3); // drives the atan2 update as a side effect → enc3LastDial is current
            int sel = constrain(getKnob3RawDial() * 4 / ENC3_DIAL_RANGE, 0, 3);
#else
            int sel = constrain(map(readKnob(3), 0, 4095, 3, 0), 0, 3);
#endif
            bool selChanged = (sel != lastSel);
            if (selChanged) {
                lastSel              = sel;
                vstSelectedOption    = sel;
                displayRefreshNeeded = true;
            }
            // RGB ring: 24 LEDs split among the 4 switch options, selected one
            // brighter, all sharing one warm-white identity color (one selector,
            // not distinct effects).
            {
                static unsigned long lastTick = 0;
                if (millis() - lastTick >= 40) {
                    lastTick = millis();
                    const CRGB warmWhite(255, 180, 110);
                    for (int i = 0; i < RGB_RING_SIZE; i++) {
                        int  k     = ((RGB_RING_SIZE - 1 - i) * 4) / RGB_RING_SIZE;
                        CRGB color = warmWhite;
                        if (k != vstSelectedOption) color.nscale8(5);
                        setRGBledRaw(RGB_RING_START + i, color);
                    }
                }
            }
            // 3-way switch drives whichever option is currently selected.
            drive3waySwitchFromControl(effectControls[OTHER][vstSwitchOptions[vstSelectedOption]],
                              modeJustEntered || selChanged);
            break;
        }
        case 4: {
            // Tutorial mode: see GETTING_STARTED.md "Solar System walkthrough" for the
            // full explanation of every line below.
            static const uint8_t solarCC[5]  = {90, 91, 92, 93, 94};
            static int           lastSent[5] = {-1, -1, -1, -1, -1};
            static int           lastSwitchSent = -1;

            // 3-way switch -> its own fixed CC, one of 3 distinct values
            // depending on position (0=up, 1=mid, 2=bottom).
            /* TUTORIAL
            static const uint8_t switchValues[3] = {0, 64, 127};
            int switchValue = switchValues[readThreeWaySwitch()];
            if (switchValue != lastSwitchSent) {
                lastSwitchSent       = switchValue;
                sendMidiCC(96, switchValue);
                displayRefreshNeeded = true; // tell drawScreenMode4() to redraw the moved star
            }
            */
            drive3waySwitchToCC(96, lastSwitchSent); //equivalent of TUTORIAL

            setRGBledRaw(LED_MODE,     CRGB(255, 200, 0));
            setRGBledRaw(LED_EFFECT,   CRGB(0, 0, 0));
            setRGBledRaw(RGB_RING,     CRGB(0, 0, 0));
            setRGBledRaw(LED_SWITCH_A, CRGB(255, 200, 0));
            setRGBledRaw(LED_SWITCH_B, CRGB(255, 200, 0));
            setRGBledRaw(LED_SWITCH_C, CRGB(255, 200, 0));
            switchCC[1] = 95; // switch B sends CC 95 on this page only

            for (int k = 0; k < 5; k++)
            /* TUTORIAL:
                int value = map(readKnob(k + 1), 0, 4095, 0, 127);
                ringMidiValues[k] = value * 255 / 127;
                if (value != lastSent[k]) {
                    lastSent[k] = value;
                    sendMidiCC(solarCC[k], value);
                    displayRefreshNeeded = true;
                }
            */
                driveKnobToCC(k + 1, k, solarCC[k], lastSent[k]);//equivalent of TUTORIAL
            break;
        }
        default:
            // Placeholder modes (5/...)
            for (int k = 0; k < 5; k++) ringMidiValues[k] = 0;
            break;
    }
}

// Redraws whichever screen is current, but only when something actually
// changed (displayRefreshNeeded) and no overlay (info pages, BLE warning popup)
// is covering it, redrawing underneath one of those would just be wasted work.
void handleDisplay() {
    if (displayRefreshNeeded && !isInInfoMode() && !isBleWarnActive()) {
        displayRefreshNeeded = false;
        drawModeScreen(currentMode);
    }
    handleInfoDisplays();
}

// Refreshes every "ambient" UI indicator: RGB rings, the cached effect-LED
// color, MIDI-feedback LEDs, battery gauge, BLE status LEDs, and the 3-way
// switch's position LEDs. None of these read user input, they just reflect
// current state, so they can all tick unconditionally every loop().
void updateUIled() {
    updateRings();
    updateEffectLedColor();
    updateLedMidiFeedback();
    updateBatteryLeds();
    updateBleStatusLeds();
    updateThreeWaySwitch();
}

// Polls the two physical pushbuttons not already handled inline elsewhere:
// the display-mode (long/short press) switch and the per-effect footswitch.
void updateUIswitches() {
    handleDisplaySwitch();
    handleEffectSwitch();
}

void loop() {
    //debugPrintGPIO();
    //debugPrintKnobs();

    updateMidi(); // process incoming MIDI; sets displayRefreshNeeded if params changed
    handleDisplay();
    updateUIled();
    updateUIswitches();

    if (!isInInfoMode()) updateModeIO(currentMode);

    checkBatteryShutdown();
    showRGBLeds(); // after all SPI display ops to avoid RMT/SPI conflict
    delay(10);
} 
