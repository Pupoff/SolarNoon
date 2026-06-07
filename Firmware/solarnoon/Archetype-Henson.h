#pragma once
#include "parameters.h"


//WARNING: each section (AMP, DELAY, etc) can have a maximum parameter of #define MAX_PARAMS 32 defined in paramters.h. If you want more, increase this value


// ── Section identifiers ───────────────────────────────────────────────────────
// Kept here alongside the data they describe, not in the generic parameters.h.
// SECTION_COUNT is a #define in parameters.h — keep it in sync if adding sections.
enum Section {
    AMP = 0, DELAY, COMPRESSOR, OVERDRIVE, BOOST, REVERB, CHORUS,
    OTHER, EQ,
    SECTION_COUNT // = 9: automatically stays correct if sections are added/removed
};

// Indices into the OTHER section — accessed via parameters[OTHER][OTHER_*]
enum OtherParamId {
    OTHER_INPUT_GAIN  = 0,
    OTHER_OUTPUT_GAIN = 1,
    OTHER_DOUBLER     = 2,
    OTHER_PARAM_OPEN  = 3,
    OTHER_ON_OFF      = 4,
    PRE_FX_SECTION    = 5,
    POST_FX_SECTION   = 6,
    CAB_SECTION       = 7
};

// Mode 3 ("OTHER controls" screen) maps these 4 on/off switches onto the
// physical 3-way switch; knob 3 picks which one is currently mapped (its
// index here is the knob-3 selection, 0-based). Default selection is
// OTHER_DOUBLER — see MODE3_DEFAULT_SWITCH_SEL.
const OtherParamId mode3SwitchOptions[4] = { PRE_FX_SECTION, POST_FX_SECTION, CAB_SECTION, OTHER_DOUBLER };
#define MODE3_DEFAULT_SWITCH_SEL 3 // index of OTHER_DOUBLER in mode3SwitchOptions[]

// ── Section colors ────────────────────────────────────────────────────────────
// One {r, g, b} entry per section, in Section enum order.
// Used for RGB LED feedback when an effect is active/selected.
// const has internal linkage in C++ — safe to define in a header.
const RGBColor sectionColors[SECTION_COUNT] = {
    {255, 0,   0}, // AMP        — red 
    {  0, 255, 0}, // DELAY      — green
    { 80, 255,  80}, // COMPRESSOR — blue
    {255,  40,   0}, // OVERDRIVE  — orange-red
    {255, 220,   0}, // BOOST      — yellow
    {160,   0, 255}, // REVERB     — purple
    {  0, 255, 220}, // CHORUS     — cyan
    {180, 180, 180}, // OTHER      — white
    {  0, 200, 100}, // EQ         — teal
};

inline void initializeParameters() {

  // ── AMP ─────────────────────────────────────────────────────────────────────
  // MIDI notes 0–21 (clean 1–7, crunch 8–14 at offset 10, lead 15–21 at offset 20)
  // Index 0  = section enable switch (label = section name)
  // Index 8  = amp type THREE_WAY_SWITCH (stored in AMP even though outside parameters_number)
  static const char* const ampLabels[3] = { "Clean", "Crunch", "Lead" };

  parameters[AMP][0]  = Switch(1,  0,  "AMP");
  parameters[AMP][1]  = Knob  (30, 1,  "Gain");      // clean
  parameters[AMP][2]  = Knob  (10, 2,  "Bass");
  parameters[AMP][3]  = Knob  (50, 3,  "Mid");
  parameters[AMP][4]  = Knob  (70, 4,  "Treble");
  parameters[AMP][5]  = Knob  (60, 5,  "Presence");
  parameters[AMP][6]  = Knob  (40, 6,  "Blend");
  parameters[AMP][7]  = Knob  (40, 7,  "Output");
  parameters[AMP][8]  = ThreeWay(1, 53, "Amp", ampLabels); // amp type selector
  parameters_number[AMP] = 8;

  parameters[AMP][11] = Knob  (30, 8,  "Gain");      // crunch
  parameters[AMP][12] = Knob  (10, 9,  "Bass");
  parameters[AMP][13] = Knob  (50, 10, "Mid");
  parameters[AMP][14] = Knob  (70, 11, "Treble");
  parameters[AMP][15] = Knob  (60, 12, "Presence");
  parameters[AMP][16] = Switch(1,  13, "Channel");
  parameters[AMP][17] = Knob  (40, 14, "Output");

  parameters[AMP][21] = Knob  (30, 15, "Gain");      // lead
  parameters[AMP][22] = Knob  (10, 16, "Bass");
  parameters[AMP][23] = Knob  (50, 17, "Mid");
  parameters[AMP][24] = Knob  (70, 18, "Treble");
  parameters[AMP][25] = Knob  (60, 19, "Presence");
  parameters[AMP][26] = Knob  (40, 20, "Blend");
  parameters[AMP][27] = Knob  (40, 21, "Output");
  //MAX_PARAMS= 32
  // ── DELAY ────────────────────────────────────────────────────────────────────
  static const char* const delayModeLabels[3] = { "Normal", "Wide", "PingPong" };
  static const char* const delayTypeLabels[3] = { "Modern", "Digital", "Diffusion" };
  static const char* const delaySyncLabels[3] = { "Free", "Daw", "Tap" };

  parameters[DELAY][0] = Switch  (1,  22, "DELAY");
  parameters[DELAY][1] = Knob    (10, 23, "Mix");
  parameters[DELAY][2] = Knob    (20, 24, "Feedback");
  parameters[DELAY][3] = Knob    (70, 25, "Low-Cut");
  parameters[DELAY][4] = Knob    (50, 26, "High-Cut");
  parameters[DELAY][5] = Knob    (60, 27, "Time");
  parameters[DELAY][6] = Knob    (70, 28, "Amount");
  parameters[DELAY][7] = ThreeWay(1,  29, "Sync", delaySyncLabels);
  parameters[DELAY][8] = ThreeWay(1,  30, "Mode", delayModeLabels);
  parameters[DELAY][9] = ThreeWay(1,  31, "Type", delayTypeLabels);
  parameters_number[DELAY] = 9;

  // ── COMPRESSOR ───────────────────────────────────────────────────────────────
  parameters[COMPRESSOR][0] = Switch(1,  32, "COMPRESSOR");
  parameters[COMPRESSOR][1] = Knob  (70, 33, "Level");
  parameters[COMPRESSOR][2] = Knob  (70, 34, "Threshold");
  parameters[COMPRESSOR][3] = Switch(1,  35, "Attack");   // slow / fast
  parameters_number[COMPRESSOR] = 3;

  // ── OVERDRIVE ────────────────────────────────────────────────────────────────
  parameters[OVERDRIVE][0] = Switch(1,  36, "OVERDRIVE");
  parameters[OVERDRIVE][1] = Knob  (70, 37, "Gain");
  parameters[OVERDRIVE][2] = Knob  (70, 38, "Tone");
  parameters[OVERDRIVE][3] = Knob  (70, 39, "Level");
  parameters_number[OVERDRIVE] = 3;

  // ── BOOST ────────────────────────────────────────────────────────────────────
  parameters[BOOST][0] = Switch(1,  40, "BOOST");
  parameters[BOOST][1] = Knob  (70, 41, "Gain");
  parameters[BOOST][2] = Knob  (70, 42, "Level");
  parameters[BOOST][3] = Knob  (70, 43, "Bass");
  parameters[BOOST][4] = Knob  (70, 44, "Treble");
  parameters_number[BOOST] = 4;

  // ── REVERB ───────────────────────────────────────────────────────────────────
  parameters[REVERB][0] = Switch(1,  45, "REVERB");
  parameters[REVERB][1] = Knob  (70, 46, "Mix");
  parameters[REVERB][2] = Knob  (70, 47, "Decay");
  parameters[REVERB][3] = Knob  (70, 48, "LowCut");
  parameters[REVERB][4] = Knob  (70, 49, "HighCut");
  parameters[REVERB][5] = Switch(1,  50, "Shimmer");
  parameters_number[REVERB] = 5;

  // ── CHORUS ───────────────────────────────────────────────────────────────────
  parameters[CHORUS][0] = Switch(1,  51, "CHORUS");
  parameters[CHORUS][1] = Knob  (70, 52, "Mix");
  parameters_number[CHORUS] = 1;

  // ── OTHER ────────────────────────────────────────────────────────────────────
  // Note: MIDI note 53 is taken by the AMP type selector above.
  parameters[OTHER][OTHER_INPUT_GAIN]  = Knob  (70, 54, "inputGain");
  parameters[OTHER][OTHER_OUTPUT_GAIN] = Knob  (70, 55, "outputGain");
  parameters[OTHER][OTHER_DOUBLER]     = Switch(0,  56, "doubler");
  parameters[OTHER][OTHER_PARAM_OPEN]  = Switch(1,  57, "param_open");
  parameters[OTHER][OTHER_ON_OFF]      = Switch(1,  58, "on_off");
  // ── AUTOMATED CONTROLS ───────────────────────────────────────────────────────
  // These correspond to enabling/disabling a whole section (the tab icons in the
  // VST: pre-FX, post-FX, cab). They are not controlled directly by the user —
  // they're driven automatically whenever an effect/amp in that section is
  // turned on, to make sure the section itself is active. Otherwise the plugin
  // silently ignores the effect: if an FX is on but its section is off, it has
  // no effect on the sound. Existing presets DO have effects left "on" inside
  // a switched-off section, see ensureSectionActive() in
  // effects.cpp, which turns those other effects back off when it activates
  // the section, so only the one the user just enabled actually runs.
  parameters[OTHER][PRE_FX_SECTION]  = Switch(1,  59, "Pre FX");
  parameters[OTHER][POST_FX_SECTION] = Switch(1,  60, "Post FX");
  parameters[OTHER][CAB_SECTION]     = Switch(1,  61, "Cab");
  parameters_number[OTHER] = 8;

  // ── EQ (9-band) ──────────────────────────────────────────────────────────────
  parameters[EQ][0] = Switch(1, 0, "EQ");
  for (int i = 1; i < 10; i++)
    parameters[EQ][i] = Slider(50, i, "Eq");
  parameters_number[EQ] = 9;


  
}
