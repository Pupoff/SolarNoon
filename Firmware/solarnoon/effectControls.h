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

#pragma once
#include "parameters.h"


//WARNING: each section (AMP, DELAY, etc) can have a maximum parameter of #define MAX_PARAMS 32 defined in paramters.h. 
// If you want more, increase this value


// ── Section identifiers ───────────────────────────────────────────────────────
// Kept here alongside the data they describe, not in the generic parameters.h.
// SECTION_COUNT is a #define in parameters.h, keep it in sync if adding sections.
enum Section {
    AMP = 0, DELAY, COMPRESSOR, OVERDRIVE, BOOST, REVERB, CHORUS,
    OTHER, EQ,
    SECTION_COUNT // = 9: automatically stays correct if sections are added/removed
};

// Sections read in mode 1; OTHER and EQ are
// accessed directly by their own self-contained modes, never through the
// effect cycle. 
const int EFFECT_COUNT = 7;

// ── Section colors ────────────────────────────────────────────────────────────
// One {r, g, b} entry per section, in Section enum order.
// Used for RGB LED feedback when an effect is active/selected.
// const has internal linkage in C++, safe to define in a header.
const RGBColor sectionColors[SECTION_COUNT] = {
    {255, 0,   0}, // AMP       , red 
    {  0, 255, 0}, // DELAY     , green
    { 80, 255,  80}, // COMPRESSOR, blue
    {255,  40,   0}, // OVERDRIVE , orange-red
    {255, 220,   0}, // BOOST     , yellow
    {160,   0, 255}, // REVERB    , purple
    {  0, 255, 220}, // CHORUS    , cyan
    {180, 180, 180}, // OTHER     , white
    {  0, 200, 100}, // EQ        , teal
};

inline void initializeEffectControlsTable() {

  // ── AMP ─────────────────────────────────────────────────────────────────────
  // MIDI notes 0–21 (clean 1–7, crunch 8–14 at offset 10, lead 15–21 at offset 20)
  // Index 0  = section enable switch (label = section name)
  // Index 8  = amp type THREE_WAY_SWITCH (stored in AMP even though outside effectControlCount)
  static const char* const ampLabels[3] = { "Clean", "Crunch", "Lead" };

  effectControls[AMP][0]  = Switch(0,  "AMP");
  effectControls[AMP][1]  = Knob  (1,  "Gain");      // clean
  effectControls[AMP][2]  = Knob  (2,  "Bass");
  effectControls[AMP][3]  = Knob  (3,  "Mid");
  effectControls[AMP][4]  = Knob  (4,  "Treble");
  effectControls[AMP][5]  = Knob  (5,  "Presence");
  effectControls[AMP][6]  = Knob  (6,  "Blend");
  effectControls[AMP][7]  = Knob  (7,  "Output");
  effectControls[AMP][8]  = ThreeWay(53, "Amp", ampLabels); // amp type selector
  effectControlCount[AMP] = 8;

  effectControls[AMP][11] = Knob  (8,  "Gain");      // crunch
  effectControls[AMP][12] = Knob  (9,  "Bass");
  effectControls[AMP][13] = Knob  (10, "Mid");
  effectControls[AMP][14] = Knob  (11, "Treble");
  effectControls[AMP][15] = Knob  (12, "Presence");
  effectControls[AMP][16] = Switch(13, "Channel");
  effectControls[AMP][17] = Knob  (14, "Output");

  effectControls[AMP][21] = Knob  (15, "Gain");      // lead
  effectControls[AMP][22] = Knob  (16, "Bass");
  effectControls[AMP][23] = Knob  (17, "Mid");
  effectControls[AMP][24] = Knob  (18, "Treble");
  effectControls[AMP][25] = Knob  (19, "Presence");
  effectControls[AMP][26] = Knob  (20, "Blend");
  effectControls[AMP][27] = Knob  (21, "Output");
  //MAX_PARAMS= 32
  // ── DELAY ────────────────────────────────────────────────────────────────────
  static const char* const delayModeLabels[3] = { "Normal", "Wide", "PingPong" };
  static const char* const delayTypeLabels[3] = { "Modern", "Digital", "Diffusion" };
  static const char* const delaySyncLabels[3] = { "Free", "Daw", "Tap" };

  effectControls[DELAY][0] = Switch  (22, "DELAY");
  effectControls[DELAY][1] = Knob    (23, "Mix");
  effectControls[DELAY][2] = Knob    (24, "Feedback");
  effectControls[DELAY][3] = Knob    (25, "Low-Cut");
  effectControls[DELAY][4] = Knob    (26, "High-Cut");
  effectControls[DELAY][5] = Knob    (27, "Time");
  effectControls[DELAY][6] = Knob    (28, "Amount");
  effectControls[DELAY][7] = ThreeWay(29, "Sync", delaySyncLabels);
  effectControls[DELAY][8] = ThreeWay(30, "Mode", delayModeLabels);
  effectControls[DELAY][9] = ThreeWay(31, "Type", delayTypeLabels);
  effectControlCount[DELAY] = 9;

  // ── COMPRESSOR ───────────────────────────────────────────────────────────────
  effectControls[COMPRESSOR][0] = Switch(32, "COMPRESSOR");
  effectControls[COMPRESSOR][1] = Knob  (33, "Level");
  effectControls[COMPRESSOR][2] = Knob  (34, "Threshold");
  effectControls[COMPRESSOR][3] = Switch(35, "Attack");   // slow / fast
  effectControlCount[COMPRESSOR] = 3;

  // ── OVERDRIVE ────────────────────────────────────────────────────────────────
  effectControls[OVERDRIVE][0] = Switch(36, "OVERDRIVE");
  effectControls[OVERDRIVE][1] = Knob  (37, "Gain");
  effectControls[OVERDRIVE][2] = Knob  (38, "Tone");
  effectControls[OVERDRIVE][3] = Knob  (39, "Level");
  effectControlCount[OVERDRIVE] = 3;

  // ── BOOST ────────────────────────────────────────────────────────────────────
  effectControls[BOOST][0] = Switch(40, "BOOST");
  effectControls[BOOST][1] = Knob  (41, "Gain");
  effectControls[BOOST][2] = Knob  (42, "Level");
  effectControls[BOOST][3] = Knob  (43, "Bass");
  effectControls[BOOST][4] = Knob  (44, "Treble");
  effectControlCount[BOOST] = 4;

  // ── REVERB ───────────────────────────────────────────────────────────────────
  effectControls[REVERB][0] = Switch(45, "REVERB");
  effectControls[REVERB][1] = Knob  (46, "Mix");
  effectControls[REVERB][2] = Knob  (47, "Decay");
  effectControls[REVERB][3] = Knob  (48, "LowCut");
  effectControls[REVERB][4] = Knob  (49, "HighCut");
  effectControls[REVERB][5] = Switch(50, "Shimmer");
  effectControlCount[REVERB] = 5;

  // ── CHORUS ───────────────────────────────────────────────────────────────────
  effectControls[CHORUS][0] = Switch(51, "CHORUS");
  effectControls[CHORUS][1] = Knob  (52, "Mix");
  effectControlCount[CHORUS] = 1;

  // ── OTHER (Archetype) ────────────────────────────────────────────────────────────────────
  // Note: MIDI note 53 is taken by the AMP type selector above.
  effectControls[OTHER][0] = Knob  (54, "inputGain");  // OTHER_INPUT_GAIN
  effectControls[OTHER][1] = Knob  (55, "outputGain"); // OTHER_OUTPUT_GAIN
  effectControls[OTHER][2] = Switch(56, "doubler");    // OTHER_DOUBLER
  effectControls[OTHER][3] = Switch(57, "param_open"); // OTHER_PARAM_OPEN
  effectControls[OTHER][4] = Switch(58, "on_off");     // OTHER_ON_OFF
  // ── AUTOMATED CONTROLS ───────────────────────────────────────────────────────
  // These correspond to enabling/disabling a whole section (the tab icons in the
  // VST: pre-FX, post-FX, cab). They are not controlled directly by the user,
  // they're driven automatically whenever an effect/amp in that section is
  // turned on, to make sure the section itself is active. Otherwise the plugin
  // silently ignores the effect: if an FX is on but its section is off, it has
  // no effect on the sound. Existing presets DO have effects left "on" inside
  // a switched-off section, see ensureSectionActive() in
  // effects.cpp, which turns those other effects back off when it activates
  // the section, so only the one the user just enabled actually runs.
  effectControls[OTHER][5] = Switch(59, "Pre FX");  // PRE_FX_SECTION
  effectControls[OTHER][6] = Switch(60, "Post FX"); // POST_FX_SECTION
  effectControls[OTHER][7] = Switch(61, "Cab");     // CAB_SECTION
  effectControlCount[OTHER] = 8;

  // ── EQ (9-band) NOT USED ──────────────────────────────────────────────────────────────
  effectControls[EQ][0] = Switch(0, "EQ");
  for (int i = 1; i < 10; i++)
    effectControls[EQ][i] = Slider(i, "Eq");
  effectControlCount[EQ] = 9;


  
}
