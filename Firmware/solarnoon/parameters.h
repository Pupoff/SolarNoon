#pragma once
#include <Arduino.h>

// ── Parameter types ───────────────────────────────────────────────────────────

enum ParamType { KNOB, SWITCH, THREE_WAY_SWITCH, SLIDER };

struct Parameter {
    int         value;
    int         midiNote;
    ParamType   type;
    const char* label;
    const char* const* labels; // option labels for THREE_WAY_SWITCH
    bool        known;         // false until we learn the real DAW-side value
                               // (via feedback or our own send) — displayed as "?"
};

// ──  MIDI ─────────────────────────────────────────────────────────
// Section names (AMP, DELAY …) and OtherParamId live in Archetype-Henson.h
// alongside the data they describe. Only the count is needed here for array sizes.

#define MAX_PARAMS 32  // max parameters per section; needed for 2D array indexing
#define MIDI_CHANNEL_SEND    2  // channel used to send CC to the DAW (1-indexed)
#define MIDI_CHANNEL_RECEIVE 3  // channel used to receive CC feedback from the DAW

// On/off switch semantics — how the plugin expects to be told to enable/disable
// a stompbox. Different plugins (or different MIDI Learn assignments within the
// same plugin) expect one of two conventions:
//   ABSOLUTE — message value mirrors the desired state: 127 = on, 0 = off.
//              The plugin sets its state directly from the value.
//   TOGGLE   — every press is a trigger that send 127 followed by 0
// Pick the one matching your plugin's mapping — wrong mode looks like "enabling
// works but disabling needs two presses" (or vice versa).
#define SWITCH_MIDI_MODE_ABSOLUTE 0
#define SWITCH_MIDI_MODE_TOGGLE   1
#define SWITCH_MIDI_MODE SWITCH_MIDI_MODE_TOGGLE


// Physical-knob ↔ parameter sync when they disagree (after a page/effect
// change, or when the DAW changes the value via feedback while we're not
// touching the knob):
//   JUMP   — the parameter snaps to wherever the knob physically sits the
//            instant it moves, even if that's far from the current value
//            (can cause an audible jump in the sound).
//   PICKUP — the knob is ignored (soft takeover) until its physical position
//            crosses or lands on the current value, then it takes control
//            smoothly from there — no jump, but "dead" until it catches up.
#define KNOB_MODE_JUMP   0
#define KNOB_MODE_PICKUP 1
#define KNOB_MODE KNOB_MODE_PICKUP

// ── Simple RGB color (no FastLED dependency) ─────────────────────────────────
// Use {r, g, b} aggregate syntax everywhere. Convert to CRGB at FastLED call sites.

struct RGBColor {
    uint8_t r, g, b;
};

// Convert CRGB (FastLED) → RGBColor, usable anywhere parameters.h is included.
// Include <FastLED.h> before parameters.h if you need this conversion.
#ifdef __INC_LED_SYSDEFS_H  // FastLED include guard — only compiled when FastLED is present
inline RGBColor toRGBColor(CRGB c) { return {c.r, c.g, c.b}; }
#endif

// ── Parameter factories ───────────────────────────────────────────────────────

inline Parameter Knob    (int v, int note, const char* label)                          { return {v, note, KNOB,             label, nullptr, false}; }
inline Parameter Switch  (int v, int note, const char* label)                          { return {v, note, SWITCH,           label, nullptr, false}; }
inline Parameter Slider  (int v, int note, const char* label)                          { return {v, note, SLIDER,           label, nullptr, false}; }
inline Parameter ThreeWay(int v, int note, const char* label, const char* const* opts) { return {v, note, THREE_WAY_SWITCH, label, opts,    false}; }

// ── Global arrays (defined in effects.cpp) ────────────────────────────────────

extern Parameter parameters[][MAX_PARAMS]; // first dim = SECTION_COUNT, defined in Archetype-Henson.h
extern int       parameters_number[];
