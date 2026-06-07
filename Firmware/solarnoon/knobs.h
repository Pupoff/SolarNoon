#pragma once
#include <Arduino.h>

void initKnobs();
int  readKnob(int knobID);            // knobID 1–5, returns 0–4095
void setKnob3EncoderPos(int v4095);   // KNOB3_IS_ENDLESS only: seed accumulator position
int  getKnob3RawDial();               // KNOB3_IS_ENDLESS only: current dial pos 0..ENC3_DIAL_RANGE-1
void debugPrintKnobs();               // prints all 5 knob raw values to Serial
void updateKnobsMidi();

#define ENC3_DIAL_RANGE 256           // resolution of the atan2 dial circle


