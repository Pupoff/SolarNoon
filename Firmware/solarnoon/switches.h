#pragma once

#define CC_SW_A 73  // MIDI CC sent by SW_A (IO37)
#define CC_SW_B 74  // MIDI CC sent by SW_B (IO4)
#define CC_SW_C 75  // MIDI CC sent by SW_C (IO5)
// Value: 127 on press, 0 on release

void initSwitches();
void updateSwitchesMidi(); 
