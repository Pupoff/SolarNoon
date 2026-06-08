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

#include "display.h"
#include "debug.h"
#include "pin.h"
#include <esp_mac.h>  // esp_read_mac(), ESP_MAC_BT
#include "effects.h"          // getPageCount, getKnobParamIdx, getSwitchParamIdx, lastKnobValues[]
#include "parameters.h"
#include "effectControls.h" // OTHER, effectControls[OTHER][2] ("doubler") (mode-1 per-page LED rebind example)
#include "midi.h"
#include "knobs.h"            // readKnob()
#include "three_way_switch.h" // threeWayPosition
#include "settings.h"         // boardSettings, saveSettings()
#include "leds_mono.h"        // forceRingRedraw()
#include "battery.h"

#define FIRMWARE_VERSION "0.1.0"

Adafruit_SH1106G display(128, 64, &SPI, OLED_DC, OLED_RST, OLED_CS);
int currentScreen = 1;

static bool inInfoMode = false;
static int  infoPage   = 1;

static const int MODE2_COLS = 2;
static int       mode2Page  = 0;

// Highlight state, tracks last manually changed param row
static int           hlSlot   = -1;   // 0-4 = knob, 5 = switch, -1 = none
static int           hlPage   = -1;   // page (0-based) when highlight was set
static int           hlEffect = -1;   // effect index when highlight was set
static unsigned long hlUntil  = 0;    // millis() deadline

#define LONG_PRESS_MS 600
#define HIGHLIGHT_MS  3000

bool isInInfoMode() { return inInfoMode; }
int  getInfoPage()  { return infoPage; }

// ── Screen drawing ─────────────────────────────────────────────────────────────

// Returns a display string for a switch/three-way param value.
static const char* switchValStr(int effect, int paramIdx, char* buf, int bufLen) {
    const Parameter& p = effectControls[effect][paramIdx];
    if (!p.known) return "?"; // real DAW-side value not learned yet (boot, or DAW changed it)
    if (p.type == THREE_WAY_SWITCH && p.labels)
        return p.labels[p.value];
    if (p.type == SWITCH)
        return p.value ? "ON" : "OFF";
    snprintf(buf, bufLen, "%d", p.value);
    return buf;
}

static const char* const ROM[5] = {"I", "II", "III", "IV", "V"};

// Numeral column: 18px wide (fits "III"), label at x=20 (2px gap), value right-justified.
// inverted=true: white background, black text (highlight for recently changed row).
void drawParamRow(int y, const char* numeral, const char* label, const char* value, bool inverted) {
    if (inverted) {
        display.fillRect(0, y - 1, 128, 9, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
    }
    display.setCursor(18 - (int)strlen(numeral) * 6, y);
    display.print(numeral);
    display.setCursor(20, y);
    display.print(label);
    display.setCursor(128 - (int)strlen(value) * 6, y);
    display.print(value);
    if (inverted) display.setTextColor(SH110X_WHITE);
}

// page: 0-based internally
static void drawPage(int page) {
    int effect    = effectSections[currentEffectIdx];
    int pageCount = getPageCount(effect);

    display.clearDisplay();

    // ── Knockout header (white rect, black text) ──────────────────────────────
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);

    // Effect name, left, bold (double print offset by 1px)
    display.setCursor(2, 1);
    display.print(effectNames[currentEffectIdx]);
    display.setCursor(3, 1);
    display.print(effectNames[currentEffectIdx]);

    // Page number, right-justified
    char pgBuf[8];
    snprintf(pgBuf, sizeof(pgBuf), "%d/%d", page + 1, pageCount);
    display.setCursor(126 - (int)strlen(pgBuf) * 6, 1);
    display.print(pgBuf);

    display.setTextColor(SH110X_WHITE); // restore for param rows

    bool hlActive = (hlSlot >= 0 && millis() <= hlUntil && hlPage == page && hlEffect == effect);

    // ── Knob params, Roman numeral + label + value, y = 11, 20, 29, 38, 47 ──
    for (int slot = 0; slot < 5; slot++) {
        int paramIdx = getKnobParamIdx(effect, page, slot);
        if (paramIdx < 0) continue;
        char valStr[8];
        if (effectControls[effect][paramIdx].known)
            snprintf(valStr, sizeof(valStr), "%d", effectControls[effect][paramIdx].value);
        else
            strcpy(valStr, "?"); // real DAW-side value not learned yet
        drawParamRow(11 + slot * 9, ROM[slot], effectControls[effect][paramIdx].label, valStr,
                     hlActive && hlSlot == slot);
    }

    // ── Switch param, y = 56 ────────────────────────────────────────────────
    int swIdx = getSwitchParamIdx(effect, page);
    if (swIdx >= 0) {
        char numBuf[4];
        const char* val = switchValStr(effect, swIdx, numBuf, sizeof(numBuf));
        drawParamRow(56, ">", effectControls[effect][swIdx].label, val,
                     hlActive && hlSlot == 5);
    }

    display.display();
}

// ── Layout 2: 2+3 grid ────────────────────────────────────────────────────────
// Slots 0(I) and 4(V) occupy the top-left and top-right regions.
// Slots 1(II), 2(III), 3(IV) are spread across the bottom row.
// Each cell: "NUMERAL VALUE" centered on top line, label centered below.

static int centerInRegion(int start, int width, int strLen) {
    return start + max(0, (width - strLen * 6) / 2);
}

// textSize: 1 or 2.  justify: -1=left  0=centre  +1=right.
// Label is truncated to fit the region width.
static void drawKnobCell(int slot, int effect, int page,
                         int regionX, int regionW, int y,
                         int textSize, int justify, bool invert) {
    int paramIdx = getKnobParamIdx(effect, page, slot);
    if (paramIdx < 0) return;

    const char* label = effectControls[effect][paramIdx].label;
    int charW    = textSize * 6;
    int charH    = textSize * 8;
    int maxChars = regionW / charW;
    int len      = min((int)strlen(label), maxChars);

    char buf[16]; strncpy(buf, label, len); buf[len] = '\0';

    int x;
    if      (justify < 0) x = regionX + 2;
    else if (justify > 0) x = regionX + regionW - len * charW - 2;
    else                  x = centerInRegion(regionX, regionW, len);

    if (invert) {
        display.fillRect(regionX, y - 1, regionW, charH + 1, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
    }
    display.setTextSize(textSize);
    display.setCursor(x, y);
    display.print(buf);
    display.setTextSize(1);
    if (invert) display.setTextColor(SH110X_WHITE);
}

static void drawPageLayout2(int page) {
    int effect    = effectSections[currentEffectIdx];
    int pageCount = getPageCount(effect);

    display.clearDisplay();

    // Header, same style as layout 1 (bold effect name, page right-justified)
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1); display.print(effectNames[currentEffectIdx]);
    display.setCursor(3, 1); display.print(effectNames[currentEffectIdx]);
    char pgBuf[8];
    snprintf(pgBuf, sizeof(pgBuf), "%d/%d", page + 1, pageCount);
    display.setCursor(126 - (int)strlen(pgBuf) * 6, 1);
    display.print(pgBuf);
    display.setTextColor(SH110X_WHITE);

    bool hlA = (hlSlot >= 0 && millis() <= hlUntil && hlPage == page && hlEffect == effect);

    // 5 equal columns of ~25px.
    // Row 1 (labels) at y=16, Row 2 (values) at y=28, switch at y=49.
    for (int slot = 0; slot < 5; slot++) {
        int paramIdx = getKnobParamIdx(effect, page, slot);
        if (paramIdx < 0) continue;

        int cx = slot * 128 / 5;
        int cw = (slot + 1) * 128 / 5 - cx;
        bool inv = hlA && hlSlot == slot;

        if (inv) {
            display.fillRect(cx, 15, cw, 22, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
        }

        // Label, truncated to column width, centred
        const char* label = effectControls[effect][paramIdx].label;
        int lLen = min((int)strlen(label), cw / 6);
        char lbuf[8]; strncpy(lbuf, label, lLen); lbuf[lLen] = '\0';
        display.setCursor(cx + max(0, (cw - lLen * 6) / 2), 16);
        display.print(lbuf);

        // Value, centred
        char vbuf[5];
        if (effectControls[effect][paramIdx].known)
            snprintf(vbuf, sizeof(vbuf), "%d", effectControls[effect][paramIdx].value);
        else
            strcpy(vbuf, "?"); // real DAW-side value not learned yet
        int vLen = strlen(vbuf);
        display.setCursor(cx + max(0, (cw - vLen * 6) / 2), 28);
        display.print(vbuf);

        if (inv) display.setTextColor(SH110X_WHITE);
    }

    // Switch row at bottom
    int swIdx = getSwitchParamIdx(effect, page);
    if (swIdx >= 0) {
        char numBuf[4];
        const char* val = switchValStr(effect, swIdx, numBuf, sizeof(numBuf));
        drawParamRow(49, ">", effectControls[effect][swIdx].label, val, hlA && hlSlot == 5);
    }

    display.display();
}

// ── Layout 3: 2+3 grid, all size-1 labels ─────────────────────────────────────
// Slots 0 and 4 fill the top row (64px each).
// Slots 1, 2, 3 fill the bottom row (~42px each).
// Switch sits close to the header; two knob rows are evenly spaced below it.

static void drawPageLayout3(int page) {
    int effect    = effectSections[currentEffectIdx];
    int pageCount = getPageCount(effect);

    display.clearDisplay();

    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1); display.print(effectNames[currentEffectIdx]);
    display.setCursor(3, 1); display.print(effectNames[currentEffectIdx]);
    char pgBuf[8];
    snprintf(pgBuf, sizeof(pgBuf), "%d/%d", page + 1, pageCount);
    display.setCursor(126 - (int)strlen(pgBuf) * 6, 1);
    display.print(pgBuf);
    display.setTextColor(SH110X_WHITE);

    bool hlA = (hlSlot >= 0 && millis() <= hlUntil && hlPage == page && hlEffect == effect);

    // Switch row, right below header
    int swIdx = getSwitchParamIdx(effect, page);
    if (swIdx >= 0) {
        char numBuf[4];
        const char* val = switchValStr(effect, swIdx, numBuf, sizeof(numBuf));
        drawParamRow(11, ">", effectControls[effect][swIdx].label, val, hlA && hlSlot == 5);
    }

    // Top row (slots 0 and 4): two equal halves, y=28
    drawKnobCell(0, effect, page,   0, 64, 28, 1, 0, hlA && hlSlot == 0);
    drawKnobCell(4, effect, page,  64, 64, 28, 1, 0, hlA && hlSlot == 4);

    // Bottom row (slots 1, 2, 3): three equal thirds, y=46
    drawKnobCell(1, effect, page,   0, 42, 46, 1, 0, hlA && hlSlot == 1);
    drawKnobCell(2, effect, page,  43, 43, 46, 1, 0, hlA && hlSlot == 2);
    drawKnobCell(3, effect, page,  86, 42, 46, 1, 0, hlA && hlSlot == 3);

    display.display();
}

// ── Layout dispatch ───────────────────────────────────────────────────────────

int pageLayout = 2; // 1 = classic list, 2 = 5-column, 3 = 2+3 grid

void drawScreen(int page) {
    int p = page - 1; // convert to 0-based
    if      (pageLayout == 2) drawPageLayout2(p);
    else if (pageLayout == 3) drawPageLayout3(p);
    else                      drawPage(p);
}

static void drawScreenInfo1() {
    display.clearDisplay();
    char buf[22];

    // ── Header ────────────────────────────────────────────────────────────────
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("INFO");
    display.setTextColor(SH110X_WHITE);

    // ── Firmware version ──────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "FW    v%s", FIRMWARE_VERSION);
    display.setCursor(0, 11);
    display.print(buf);

    // ── Battery ───────────────────────────────────────────────────────────────
    //snprintf(buf, sizeof(buf), "BATT  %4.2fV  (%d%%)", getBatteryVoltage(), getBatteryPercent());
    snprintf(buf, sizeof(buf), "BATT  %4.2fV", getBatteryVoltage());
    display.setCursor(0, 20);
    display.print(buf);

    // ── MIDI USB ──────────────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "USB   %s", (bool)Serial ? "connected" : "offline");
    display.setCursor(0, 29);
    display.print(buf);

    if (isMidiMounted())
        snprintf(buf, sizeof(buf), "MIDI  OK/%s", getFeedbackOk() ? "feedback" : "no feedback");
    else
        snprintf(buf, sizeof(buf), "MIDI  offline");
    display.setCursor(0, 38);
    display.print(buf);

    // ── Uptime ────────────────────────────────────────────────────────────────
    unsigned long s = millis() / 1000;
    if      (s < 60)   snprintf(buf, sizeof(buf), "UP    %lus", s);
    else if (s < 3600) snprintf(buf, sizeof(buf), "UP    %lum %02lus", s / 60, s % 60);
    else               snprintf(buf, sizeof(buf), "UP    %luh %02lum", s / 3600, (s % 3600) / 60);
    display.setCursor(0, 56);
    display.print(buf);

    display.display();
}

static void drawScreenInfo2() {
    display.clearDisplay();

    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("BRIGHTNESS");
    display.setTextColor(SH110X_WHITE);

    // Brightness rows: I II IV V, evenly at y = 11, 20, 29, 38
    char valStr[8];
    snprintf(valStr, sizeof(valStr), "%d%%", boardSettings.ringMonoBrightness);
    drawParamRow(11, "I",  "IS31 rings",  valStr);
    snprintf(valStr, sizeof(valStr), "%d%%", boardSettings.rgbRingBrightness);
    drawParamRow(20, "II", "RGB ring",    valStr);
    snprintf(valStr, sizeof(valStr), "%d%%", boardSettings.rgbSwitchBrightness);
    drawParamRow(29, "IV", "Switches",    valStr);
    snprintf(valStr, sizeof(valStr), "%d%%", boardSettings.displayContrast);
    drawParamRow(38, "V",  "Display",     valStr);

    // Separator + knob 3 speed at the bottom
    display.drawFastHLine(0, 48, 128, SH110X_WHITE);
    snprintf(valStr, sizeof(valStr), "%d", boardSettings.enc3Sensitivity);
    drawParamRow(51, "III", "K3 speed",   valStr);

    display.display();
}

static void drawScreenInfo3() {
    display.clearDisplay();

    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("LAYOUT");
    display.setTextColor(SH110X_WHITE);

    drawParamRow(20, "1", "Classic list", "", pageLayout == 1);
    drawParamRow(34, "2", "5-column",     "", pageLayout == 2);
    drawParamRow(48, "3", "2+3 grid",     "", pageLayout == 3);

    display.display();
}

// ── Info page 2: BLE ──────────────────────────────────────────────────────────

static void drawScreenInfoBLE() {
    display.clearDisplay();
    char buf[22];

    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("BLE INFO");
    display.setTextColor(SH110X_WHITE);

    // Boot transport
    snprintf(buf, sizeof(buf), "Boot  %s", isBleMode() ? "BLE" : "USB");
    display.setCursor(0, 12);
    display.print(buf);


    // Our full BT MAC, "MAC AA:BB:CC:DD:EE:FF" = 21 chars = 126px (fits)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(buf, sizeof(buf), "MAC %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    display.setCursor(0, 21);
    display.print(buf);

        // Conn + Bond on one line: "Conn YES  Bond YES" = 18 chars = 108px
    BlePeerInfo peer = getBlePeerInfo();
    bool connected = isMidiMounted();
    if (isBleMode())
        snprintf(buf, sizeof(buf), "Conn %-3s  Bond %-3s",
                 connected   ? "YES" : "NO",
                 peer.bonded ? "YES" : "NO");
    else
        snprintf(buf, sizeof(buf), "Conn N/A  Bond N/A");
    display.setCursor(0, 30);
    display.print(buf);
    // Peer: first 3 bytes ("AA:BB:CC") on line 4, last 3 bytes on line 5
    // peer.addr is "AA:BB:CC:DD:EE:FF" (17 chars), or "---"
    snprintf(buf, sizeof(buf), "To: %.18s", peer.addr); // 
    display.setCursor(0, 39);
    display.print(buf);


    // Bottom line: inverted warning if switch diverged from boot
    bool swOn = digitalRead(PIN_BLE_SW) == HIGH;
    if (swOn != isBleMode()) {
        display.fillRect(0, 56, 128, 8, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
        display.setCursor(0, 57);
        display.print(swOn ? "SW ON  - reboot!" : "SW OFF - reboot!");
        display.setTextColor(SH110X_WHITE);
    }

    display.display();
}

// Info pages 1 (status) and 2 (BLE) both just redraw their static screen every
// second while visible, collapse the two near-identical tickers into one,
// switching on infoPage for which draw function to call.
void updateInfoLiveRedraw() {
    if (!inInfoMode || (infoPage != 1 && infoPage != 2)) return;
    static unsigned long lastDraw = 0;
    unsigned long now = millis();
    if (now - lastDraw < 1000) return;
    lastDraw = now;
    switch (infoPage) {
        case 1: drawScreenInfo1();  break;
        case 2: drawScreenInfoBLE(); break;
    }
}

// State below is shared between updateInfo2Controls() (per-tick, called only
// while info page 3 is showing) and finishInfo2Controls() (called once, by the
// loop's info-page switch, on the tick we leave that page), hence file scope.
static bool          info2Active       = false;
static int           info2InitKnob[5]  = {0,0,0,0,0};
static int           info2LastKnob[5]  = {0,0,0,0,0};
static bool          info2TakenOver[5] = {false,false,false,false,false};
static bool          info2Dirty        = false;
static unsigned long info2DirtyTime    = 0;

void updateInfo2Controls() {
    static unsigned long lastRun = 0;
    unsigned long now = millis();
    if (now - lastRun < 20) return;
    lastRun = now;

    if (!info2Active) {
        info2InitKnob[0] = map(readKnob(1), 0, 4095, 0, 100);
        info2InitKnob[1] = map(readKnob(2), 0, 4095, 0, 100);
#ifdef KNOB3_IS_ENDLESS
        setKnob3EncoderPos(boardSettings.enc3Sensitivity * 4095 / 100);
#endif
        info2InitKnob[2] = map(readKnob(3), 0, 4095, 0, 100);
        info2InitKnob[3] = map(readKnob(4), 0, 4095, 0, 100);
        info2InitKnob[4] = map(readKnob(5), 0, 4095, 0, 100);
        for (int i = 0; i < 5; i++) { info2LastKnob[i] = info2InitKnob[i]; info2TakenOver[i] = false; }
        info2Active = true;
    }

    bool changed = false;

    int k1 = map(readKnob(1), 0, 4095, 0, 100);
    if (!info2TakenOver[0] && abs(k1 - info2InitKnob[0]) >= 2) info2TakenOver[0] = true;
    if (info2TakenOver[0] && k1 != info2LastKnob[0]) { info2LastKnob[0] = k1; boardSettings.ringMonoBrightness = (uint8_t)k1; forceRingRedraw(); changed = true; }

    int k2 = map(readKnob(2), 0, 4095, 0, 100);
    if (!info2TakenOver[1] && abs(k2 - info2InitKnob[1]) >= 2) info2TakenOver[1] = true;
    if (info2TakenOver[1] && k2 != info2LastKnob[1]) { info2LastKnob[1] = k2; boardSettings.rgbRingBrightness = (uint8_t)k2; changed = true; }

    int k3 = map(readKnob(3), 0, 4095, 0, 100);
    if (!info2TakenOver[2] && abs(k3 - info2InitKnob[2]) >= 2) info2TakenOver[2] = true;
    if (info2TakenOver[2] && k3 != info2LastKnob[2]) { info2LastKnob[2] = k3; boardSettings.enc3Sensitivity = (uint8_t)k3; changed = true; }

    int k4 = map(readKnob(4), 0, 4095, 0, 100);
    if (!info2TakenOver[3] && abs(k4 - info2InitKnob[3]) >= 2) info2TakenOver[3] = true;
    if (info2TakenOver[3] && k4 != info2LastKnob[3]) { info2LastKnob[3] = k4; boardSettings.rgbSwitchBrightness = (uint8_t)k4; changed = true; }

    int k5 = map(readKnob(5), 0, 4095, 0, 100);
    if (!info2TakenOver[4] && abs(k5 - info2InitKnob[4]) >= 2) info2TakenOver[4] = true;
    if (info2TakenOver[4] && k5 != info2LastKnob[4]) {
        info2LastKnob[4] = k5;
        boardSettings.displayContrast = (uint8_t)k5;
        display.setContrast((uint8_t)(k5 * 255 / 100));
        changed = true;
    }

    if (changed) { drawScreenInfo2(); info2Dirty = true; info2DirtyTime = now; }

    if (info2Dirty && millis() - info2DirtyTime >= 2000) {
        saveSettings();
        info2Dirty = false;
    }
}

void finishInfo2Controls() {
    if (info2Active && info2Dirty) { saveSettings(); info2Dirty = false; }
    info2Active = false; // forces a fresh knob-takeover sync on next entry
}

// Shared with finishInfo3Controls(), see info2Active comment above for why.
static bool info3Active  = false;
static int  info3LastPos = -1;

void updateInfo3Controls() {
    if (!info3Active) {
        // UP decrements threeWayPosition, DOWN increments, invert to map UP→higher layout
        threeWayPosition = constrain(3 - pageLayout, 0, 2);
        info3LastPos = threeWayPosition;
        info3Active  = true;
    }

    if (threeWayPosition != info3LastPos) {
        info3LastPos = threeWayPosition;
        pageLayout = constrain(3 - threeWayPosition, 1, 3);
        boardSettings.layoutId = (uint8_t)pageLayout;
        saveSettings();
        drawScreenInfo3();
    }
}

void finishInfo3Controls() {
    info3Active = false; // forces threeWayPosition↔pageLayout resync on next entry
}

// Top-level info-display tick, call once per loop(). Switches on which info
// page is showing so it's obvious at a glance which controller is live for the
// current state, and catches the page we just LEFT to flush/reset it (each
// controller keeps its own knob-takeover / sync state, see finishInfoX above).
void handleInfoDisplays() {
    static int lastInfoPage = 0; // 0 = not in info mode
    int infoPg = isInInfoMode() ? getInfoPage() : 0;
    if (infoPg != lastInfoPage) {
        if (lastInfoPage == 3) finishInfo2Controls();
        if (lastInfoPage == 4) finishInfo3Controls();
        lastInfoPage = infoPg;
    }
    switch (infoPg) {
        case 1:
        case 2: updateInfoLiveRedraw(); break;
        case 3: updateInfo2Controls();  break;
        case 4: updateInfo3Controls();  break;
    }

    tickHighlight();
    updateBleWarning();
}

// ── Display switch ─────────────────────────────────────────────────────────────

void handleDisplaySwitch() {
    static bool          lastReading = HIGH;
    static bool          lastStable  = HIGH;
    static unsigned long lastChange  = 0;
    static unsigned long pressStart  = 0;
    static bool          longFired   = false;

    bool          reading = digitalRead(PIN_DISPLAY_SWITCH);
    unsigned long now     = millis();

    if (reading != lastReading) { lastChange = now; lastReading = reading; }

    if (now - lastChange >= 50) {
        if (lastStable == HIGH && reading == LOW) {
            pressStart = now;
            longFired  = false;
        }

        if (lastStable == LOW && reading == HIGH && !longFired) {
            if (inInfoMode) {
                if      (infoPage == 1) { infoPage = 2; drawScreenInfoBLE(); }
                else if (infoPage == 2) { infoPage = 3; drawScreenInfo2(); }
                else if (infoPage == 3) { infoPage = 4; drawScreenInfo3(); }
                else                   { inInfoMode = false; drawModeScreen(currentMode); }
            } else if (currentMode == 2) {
                const int perPage  = MODE2_COLS * 2;
                const int numPages = (EFFECT_COUNT + perPage - 1) / perPage;
                if (numPages > 1) {
                    mode2Page = (mode2Page + 1) % numPages;
                    drawScreenMode2();
                }
            } else if (currentMode == 1) {
                int effect    = effectSections[currentEffectIdx];
                int pageCount = getPageCount(effect);
                if (pageCount > 1) { // single-page: nothing to cycle
                    currentScreen = (currentScreen % pageCount) + 1; // wraps last→1
                    for (int k = 0; k < 5; k++) lastKnobValues[k] = -1;
                    drawScreen(currentScreen);
                }
            }
        }

        if (reading == LOW && !longFired && now - pressStart >= LONG_PRESS_MS) {
            longFired = true;
            if (inInfoMode) {
                inInfoMode = false;
                drawModeScreen(currentMode);
            } else {
                inInfoMode = true; infoPage = 1; drawScreenInfo1();
            }
        }

        lastStable = reading;
    }
}

void notifyParamChanged(int slot) {
    hlSlot   = slot;
    hlPage   = currentScreen - 1;
    hlEffect = effectSections[currentEffectIdx];
    hlUntil  = millis() + HIGHLIGHT_MS;
}

void tickHighlight() {
    if (hlSlot >= 0 && millis() > hlUntil && !isInInfoMode() && currentMode == 1) {
        hlSlot = -1;
        drawScreen(currentScreen);
    }
}

// ── Mode 2 overview ───────────────────────────────────────────────────────────
// Boxes per row: MODE2_COLS. perPage = MODE2_COLS*2. Extra effects → next page.
// Enabled → filled rounded rect (inverted). Selected → bar at bottom.
// Page indicator dots drawn at bottom when numPages > 1.

static void drawMode2Box(int x, int y, int w, int h, bool enabled, bool selected, const char* name) {
    const int r = 3;
    if (enabled) {
        display.fillRoundRect(x, y, w, h, r, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
    } else {
        display.drawRoundRect(x, y, w, h, r, SH110X_WHITE);
        display.setTextColor(SH110X_WHITE);
    }
    int textW = (int)strlen(name) * 6;
    display.setCursor(x + (w - textW) / 2, y + (h - 8) / 2 - 2);
    display.print(name);
    if (selected) {
        uint16_t barCol = enabled ? SH110X_BLACK : SH110X_WHITE;
        display.fillRoundRect(x + 4, y + h - 5, w - 8, 3, 1, barCol);
    }
    display.setTextColor(SH110X_WHITE);
}

void drawScreenMode2() {
    static int lastMode = -1;
    if (lastMode != currentMode) { mode2Page = 0; lastMode = currentMode; }

    const int perPage  = MODE2_COLS * 2;
    const int numPages = (EFFECT_COUNT + perPage - 1) / perPage;
    const int boxW     = (126 - (MODE2_COLS - 1) * 2) / MODE2_COLS;
    const int boxH     = (numPages > 1) ? 27 : 30;
    const int row2Y    = 1 + boxH + 2;

    display.clearDisplay();
    display.setTextSize(1);

    int start = mode2Page * perPage;
    int end   = min(start + perPage, EFFECT_COUNT);
    for (int k = start; k < end; k++) {
        int  local   = k - start;
        int  row     = local / MODE2_COLS;
        int  col     = local % MODE2_COLS;
        int  x       = 1 + col * (boxW + 2);
        int  y       = (row == 0) ? 1 : row2Y;
        int  effect  = effectSections[k];
        bool enabled = isEffectActiveForDisplay(effect);
        bool sel     = (k == mode2SelectedEffect);
        drawMode2Box(x, y, boxW, boxH, enabled, sel, effectNames[k]);
    }

    if (numPages > 1) {
        int dotSpan = numPages * 4 - 1;
        int dotX    = 64 - dotSpan / 2;
        for (int p = 0; p < numPages; p++) {
            int dx = dotX + p * 4;
            if (p == mode2Page) display.fillRect(dx, 60, 3, 3, SH110X_WHITE);
            else                display.drawRect(dx, 60, 3, 3, SH110X_WHITE);
        }
    }

    display.display();
}

// Failsafe shown by drawModeScreen()'s default case (in midi_a14h.ino): a slot
// exists in modeEnabled[] (and so can be cycled to) but has no real screen
// yet. Makes "forgot to add a case" an obvious placeholder on the hardware
// instead of a blank/stuck display. Not static: drawModeScreen() needs it.
void drawScreenModeGeneric(int mode) {
    display.clearDisplay();
    char buf[22];

    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("MODE");
    display.setTextColor(SH110X_WHITE);

    snprintf(buf, sizeof(buf), "Mode %d", mode);
    display.setCursor(0, 28);
    display.print(buf);
    display.setCursor(0, 40);
    display.print("not configured");

    display.display();
}

void updateMode2Controls() {
    static int lastSel = -1;
    const int perPage  = MODE2_COLS * 2;

#ifdef KNOB3_IS_ENDLESS
    // Mode 2: 1 physical turn = full effect sweep, ignore user sensitivity setting.
    readKnob(3); // drives the atan2 update as a side effect → enc3LastDial is current
    int sel = constrain(getKnob3RawDial() * EFFECT_COUNT / ENC3_DIAL_RANGE, 0, EFFECT_COUNT - 1);
#else
    int sel = constrain(map(readKnob(3), 0, 4095, EFFECT_COUNT - 1, 0), 0, EFFECT_COUNT - 1);
#endif

    if (sel != lastSel) {
        mode2SelectedEffect = sel;
        lastSel             = sel;
        int newPage = sel / perPage;
        if (newPage != mode2Page) mode2Page = newPage;
        drawScreenMode2();
    }
}

void initDisplay() {
    SPI.begin(OLED_SCK, -1, OLED_MOSI);
    if (!display.begin(0, true)) {
        DBG_PRINTLN("SH1106G not found");
        while (1);
    }
    display.setRotation(2); // 180°, adjust to 0 if screen is mounted the other way
    display.setContrast((uint8_t)(boardSettings.displayContrast * 255 / 100));
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    pageLayout = constrain((int)boardSettings.layoutId, 1, 3);
    display.clearDisplay();
    display.display();
}

// ── BLE warning popup ─────────────────────────────────────────────────────────
// Shown when the BLE switch is flipped after boot, since the transport is fixed
// at startup. Auto-dismisses after BLE_WARN_MS ms.

#define BLE_WARN_MS 5000

static bool          bleWarnActive     = false;
static unsigned long bleWarnUntil      = 0;
static bool          bleWarnSwitchIsOn = false; // the new switch state that triggered it

bool isBleWarnActive() { return bleWarnActive; }

static void drawBleWarningPopup() {
    display.clearDisplay();

    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("! BLE WARNING !");
    display.setTextColor(SH110X_WHITE);

    if (bleWarnSwitchIsOn) {
        // Booted in USB mode, switch now ON
        display.setCursor(0, 14);
        display.print("Reboot to enable");
        display.setCursor(0, 24);
        display.print("BLE MIDI.");
        display.setCursor(0, 38);
        display.print("USB MIDI active");
        display.setCursor(0, 48);
        display.print("until reboot.");
    } else {
        // Booted in BLE mode, switch now OFF
        display.setCursor(0, 14);
        display.print("BLE disabled.");
        display.setCursor(0, 24);
        display.print("No MIDI until");
        display.setCursor(0, 34);
        display.print("reboot.");
        display.setCursor(0, 48);
        display.print("Reboot for USB.");
    }

    display.display();
}

void triggerBleWarning(bool switchIsNowOn) {
    bleWarnActive       = true;
    bleWarnSwitchIsOn   = switchIsNowOn;
    bleWarnUntil        = millis() + BLE_WARN_MS;
    drawBleWarningPopup();
}

void updateBleWarning() {
    if (!bleWarnActive) return;
    if (millis() < bleWarnUntil) return;
    bleWarnActive = false;
    if (isInInfoMode()) return; // updateInfoLiveRedraw() redraws within 1 s
    drawModeScreen(currentMode);
}

void drawScreenBatteryDead() {
    display.clearDisplay();

    // Header
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("LOW BATTERY");
    display.setTextColor(SH110X_WHITE);

    // Battery body (horizontal)
    const int bx = 12, by = 16, bw = 88, bh = 26;
    display.drawRect(bx, by, bw, bh, SH110X_WHITE);
    // Positive terminal on the right
    display.fillRect(bx + bw, by + bh / 2 - 5, 6, 10, SH110X_WHITE);
    // X inside (dead / empty indicator)
    display.drawLine(bx + 3, by + 3,  bx + bw - 4, by + bh - 4, SH110X_WHITE);
    display.drawLine(bx + bw - 4, by + 3, bx + 3, by + bh - 4, SH110X_WHITE);

    display.setCursor(4, 50);
    display.print("Shutting down...");
    display.display();
}
