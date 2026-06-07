#pragma once
#include <Adafruit_SH110X.h>

extern Adafruit_SH1106G display;
extern int currentScreen; // current parameter page (1-based, unbounded)

extern int pageLayout; // 1 = classic list, 2 = 2+3 grid

void initDisplay();
void drawScreen(int page); // renders page N of the active effect's parameters
void handleDisplaySwitch();
bool isInInfoMode(); // true while info screens are active
int  getInfoPage();  // 1/2/3 — only valid when isInInfoMode() is true
void updateInfo1Live();        // refresh info page 1 every second while visible
void updateInfoBLELive();      // refresh info page 2 (BLE) every second while visible
void updateInfo2Controls();    // read knobs/switch in info page 3 and update board settings
void updateInfo3Controls();    // read switch in info page 4 and select layout
void drawScreenMode2();        // mode 2 overview: 2-row effect grid
void drawScreenMode3();        // mode 3: OTHER-section controls screen
void drawScreenBatteryDead();  // low-battery warning shown before deep sleep
void updateMode2Controls();    // read knob 3 in mode 2, update selection
void updateMode3Controls();    // read knob 3 in mode 3, update OTHER-switch selection
void notifyParamChanged(int slot); // slot 0-4 = knob, 5 = switch — triggers timed highlight
void tickHighlight();              // call in loop() — clears highlight after HIGHLIGHT_MS

// BLE switch warning popup — shown when switch state diverges from boot-time transport
void triggerBleWarning(bool switchIsNowOn); // call once on switch change
void updateBleWarning();                    // call in loop() — auto-dismisses after timeout
bool isBleWarnActive();                     // true while popup is displayed
