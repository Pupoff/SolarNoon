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
#include <Adafruit_SH110X.h>

extern Adafruit_SH1106G display;
extern int currentScreen; // current parameter page (1-based, unbounded)

extern int pageLayout; // 1 = classic list, 2 = 2+3 grid

void initDisplay();
void drawScreen(int page); // renders page N of the active effect's effectControls
void handleDisplaySwitch();
bool isInInfoMode(); // true while info screens are active
int  getInfoPage();  // 1/2/3, only valid when isInInfoMode() is true
void updateInfoLiveRedraw();   // refresh info pages 1 (status) and 2 (BLE) every second while visible
void updateInfo2Controls();    // read knobs/switch in info page 3 and update board settings, call only while on that page
void updateInfo3Controls();    // read switch in info page 4 and select layout, call only while on that page
void finishInfo2Controls();    // call once when leaving info page 3, flushes pending settings save, resets takeover state
void finishInfo3Controls();    // call once when leaving info page 4, resets sync state for next entry
void handleInfoDisplays();     // call in loop(), dispatches the live info-page controller, flushes the one we just left, then ticks highlight + BLE warning
void drawScreenMode2();        // mode 2 overview: 2-row effect grid
void drawScreenArchetypeVSTglobalcontrols(); // mode 3: OTHER-section controls screen, defined in midi_a14h.ino (self-contained mode, see GETTING_STARTED.md)
// Shared param-row layout (numeral column, label, right-justified value, optional
// inverted highlight) used by the parameter-view pages and available to other
// self-contained mode screens (e.g. mode 3) so they don't need their own copy.
void drawParamRow(int y, const char* numeral, const char* label, const char* value, bool inverted = false);
void drawScreenModeGeneric(int mode); // failsafe placeholder for an enabled slot with no real screen, used by drawModeScreen() in midi_a14h.ino
void drawModeScreen(int mode); // dispatches to the right mode screen, or drawScreenModeGeneric() for unconfigured slots, defined in midi_a14h.ino so the mode->screen mapping is easy to find/edit
void drawScreenBatteryDead();  // low-battery warning shown before deep sleep
void updateMode2Controls();    // read knob 3 in mode 2, update selection
void notifyParamChanged(int slot); // slot 0-4 = knob, 5 = switch, triggers timed highlight
void tickHighlight();              // call in loop(), clears highlight after HIGHLIGHT_MS

// BLE switch warning popup, shown when switch state diverges from boot-time transport
void triggerBleWarning(bool switchIsNowOn); // call once on switch change
void updateBleWarning();                    // call in loop(), auto-dismisses after timeout
bool isBleWarnActive();                     // true while popup is displayed
