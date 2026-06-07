#pragma once
#include "parameters.h"
#include "leds_rgb.h"  // for CRGB return type of getEffectColor()

extern const int         effectSections[];
extern const char* const effectNames[];
extern const int         EFFECT_COUNT;
extern int               currentEffectIdx;
extern int               currentMode;            // 1=white 2=orange 3=red
extern int               mode2SelectedEffect;    // 0-based effect index selected by knob 3
extern int               mode3SelectedParam;     // 0-based index into mode3SwitchOptions[] selected by knob 3
extern int               lastKnobValues[5];

// Page layout helpers — used by display.cpp too
int  getPageCount    (int effect);
int  getKnobParamIdx (int effect, int page, int slot); // 0-based page/slot; -1 = empty
int  getSwitchParamIdx(int effect, int page);           // -1 = no switch on this page

void initParameters();
void initMode();         // load currentMode from boardSettings
CRGB getEffectColor();          // returns the active section's color as CRGB
CRGB getEffectColorByIdx(int idx); // same, for any 0-based effect index
CRGB getModeColor();     // white / orange / red for mode 1/2/3
void applyEffectColor(); // sets LED_EFFECT to the active section's color
void updateEffectLeds();
void updateModeLed();    // cached — safe to call every loop()
void updateRingByMode(); // mode 1: subtle rainbow drift on RGB ring
void handleEffectSwitch();

// Henson groups stompboxes into sections toggled via the VST's tab icons
// (CAB for the amp, PRE-FX for boost/overdrive/compressor, POST-FX for
// chorus/reverb/delay) — an effect that's "on" while its section is "off" is
// silently ignored by the plugin. Call this right after turning an effect ON
// to make sure its section is active too (turns it on + sends MIDI if not).
void ensureSectionActive(int effect);

// Display-only check: an effect reads as "on" only when its switch AND its
// section are both active (otherwise the plugin ignores it silently). Used by
// LED feedback and the mode-2 grid so the UI doesn't show a lie while an
// effect's .value is still "on" but its section just got switched off.
bool isEffectActiveForDisplay(int effect);

// Toggles an effect's on/off switch — the single source of truth for every
// "turn this effect on/off" button. Handles the case where the effect's
// switch is on but its section is off (see isEffectActiveForDisplay): wakes
// the section instead of toggling the effect off again.
void toggleEffect(int effect);
