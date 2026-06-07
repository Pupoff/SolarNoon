#pragma once

extern int threeWayPosition; // 0 = up, 1 = middle, 2 = bottom

void initThreeWaySwitch();
void updateThreeWaySwitch();
void updateSwitchMidi(); // reads threeWayPosition, sends MIDI for switch on current page
void updateMode3Switch(); // mode 3: drives whichever OTHER switch knob 3 has selected
