#include "display.h"
#include "debug.h"
#include "pin.h"
#include <esp_mac.h>  // esp_read_mac(), ESP_MAC_BT
#include "effects.h"          // getPageCount, getKnobParamIdx, getSwitchParamIdx, lastKnobValues[]
#include "parameters.h"
#include "Archetype-Henson.h" // OTHER, OTHER_*, mode3SwitchOptions[] — mode 3 screen
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

// Highlight state — tracks last manually changed param row
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
    const Parameter& p = parameters[effect][paramIdx];
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
static void drawParamRow(int y, const char* numeral, const char* label, const char* value, bool inverted = false) {
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

    // Effect name — left, bold (double print offset by 1px)
    display.setCursor(2, 1);
    display.print(effectNames[currentEffectIdx]);
    display.setCursor(3, 1);
    display.print(effectNames[currentEffectIdx]);

    // Page number — right-justified
    char pgBuf[8];
    snprintf(pgBuf, sizeof(pgBuf), "%d/%d", page + 1, pageCount);
    display.setCursor(126 - (int)strlen(pgBuf) * 6, 1);
    display.print(pgBuf);

    display.setTextColor(SH110X_WHITE); // restore for param rows

    bool hlActive = (hlSlot >= 0 && millis() <= hlUntil && hlPage == page && hlEffect == effect);

    // ── Knob params — Roman numeral + label + value, y = 11, 20, 29, 38, 47 ──
    for (int slot = 0; slot < 5; slot++) {
        int paramIdx = getKnobParamIdx(effect, page, slot);
        if (paramIdx < 0) continue;
        char valStr[8];
        if (parameters[effect][paramIdx].known)
            snprintf(valStr, sizeof(valStr), "%d", parameters[effect][paramIdx].value);
        else
            strcpy(valStr, "?"); // real DAW-side value not learned yet
        drawParamRow(11 + slot * 9, ROM[slot], parameters[effect][paramIdx].label, valStr,
                     hlActive && hlSlot == slot);
    }

    // ── Switch param — y = 56 ────────────────────────────────────────────────
    int swIdx = getSwitchParamIdx(effect, page);
    if (swIdx >= 0) {
        char numBuf[4];
        const char* val = switchValStr(effect, swIdx, numBuf, sizeof(numBuf));
        drawParamRow(56, ">", parameters[effect][swIdx].label, val,
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

    const char* label = parameters[effect][paramIdx].label;
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

    // Header — same style as layout 1 (bold effect name, page right-justified)
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

        // Label — truncated to column width, centred
        const char* label = parameters[effect][paramIdx].label;
        int lLen = min((int)strlen(label), cw / 6);
        char lbuf[8]; strncpy(lbuf, label, lLen); lbuf[lLen] = '\0';
        display.setCursor(cx + max(0, (cw - lLen * 6) / 2), 16);
        display.print(lbuf);

        // Value — centred
        char vbuf[5];
        if (parameters[effect][paramIdx].known)
            snprintf(vbuf, sizeof(vbuf), "%d", parameters[effect][paramIdx].value);
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
        drawParamRow(49, ">", parameters[effect][swIdx].label, val, hlA && hlSlot == 5);
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

    // Switch row — right below header
    int swIdx = getSwitchParamIdx(effect, page);
    if (swIdx >= 0) {
        char numBuf[4];
        const char* val = switchValStr(effect, swIdx, numBuf, sizeof(numBuf));
        drawParamRow(11, ">", parameters[effect][swIdx].label, val, hlA && hlSlot == 5);
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

    // Brightness rows: I II IV V — evenly at y = 11, 20, 29, 38
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

void updateInfo1Live() {
    if (!inInfoMode || infoPage != 1) return;
    static unsigned long lastDraw = 0;
    unsigned long now = millis();
    if (now - lastDraw >= 1000) { lastDraw = now; drawScreenInfo1(); }
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


    // Our full BT MAC — "MAC AA:BB:CC:DD:EE:FF" = 21 chars = 126px (fits)
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

void updateInfoBLELive() {
    if (!inInfoMode || infoPage != 2) return;
    static unsigned long lastDraw = 0;
    unsigned long now = millis();
    if (now - lastDraw >= 1000) { lastDraw = now; drawScreenInfoBLE(); }
}

void updateInfo2Controls() {
    static bool          active       = false;
    static int           initKnob[5]  = {0,0,0,0,0};
    static int           lastKnob[5]  = {0,0,0,0,0};
    static bool          takenOver[5] = {false,false,false,false,false};
    static bool          dirty        = false;
    static unsigned long dirtyTime    = 0;

    if (!inInfoMode || infoPage != 3) {
        if (active && dirty) { saveSettings(); dirty = false; }
        active = false;
        return;
    }

    static unsigned long lastRun = 0;
    unsigned long now = millis();
    if (now - lastRun < 20) return;
    lastRun = now;

    if (!active) {
        initKnob[0] = map(readKnob(1), 0, 4095, 0, 100);
        initKnob[1] = map(readKnob(2), 0, 4095, 0, 100);
#ifdef KNOB3_IS_ENDLESS
        setKnob3EncoderPos(boardSettings.enc3Sensitivity * 4095 / 100);
#endif
        initKnob[2] = map(readKnob(3), 0, 4095, 0, 100);
        initKnob[3] = map(readKnob(4), 0, 4095, 0, 100);
        initKnob[4] = map(readKnob(5), 0, 4095, 0, 100);
        for (int i = 0; i < 5; i++) { lastKnob[i] = initKnob[i]; takenOver[i] = false; }
        active = true;
    }

    bool changed = false;

    int k1 = map(readKnob(1), 0, 4095, 0, 100);
    if (!takenOver[0] && abs(k1 - initKnob[0]) >= 2) takenOver[0] = true;
    if (takenOver[0] && k1 != lastKnob[0]) { lastKnob[0] = k1; boardSettings.ringMonoBrightness = (uint8_t)k1; forceRingRedraw(); changed = true; }

    int k2 = map(readKnob(2), 0, 4095, 0, 100);
    if (!takenOver[1] && abs(k2 - initKnob[1]) >= 2) takenOver[1] = true;
    if (takenOver[1] && k2 != lastKnob[1]) { lastKnob[1] = k2; boardSettings.rgbRingBrightness = (uint8_t)k2; changed = true; }

    int k3 = map(readKnob(3), 0, 4095, 0, 100);
    if (!takenOver[2] && abs(k3 - initKnob[2]) >= 2) takenOver[2] = true;
    if (takenOver[2] && k3 != lastKnob[2]) { lastKnob[2] = k3; boardSettings.enc3Sensitivity = (uint8_t)k3; changed = true; }

    int k4 = map(readKnob(4), 0, 4095, 0, 100);
    if (!takenOver[3] && abs(k4 - initKnob[3]) >= 2) takenOver[3] = true;
    if (takenOver[3] && k4 != lastKnob[3]) { lastKnob[3] = k4; boardSettings.rgbSwitchBrightness = (uint8_t)k4; changed = true; }

    int k5 = map(readKnob(5), 0, 4095, 0, 100);
    if (!takenOver[4] && abs(k5 - initKnob[4]) >= 2) takenOver[4] = true;
    if (takenOver[4] && k5 != lastKnob[4]) {
        lastKnob[4] = k5;
        boardSettings.displayContrast = (uint8_t)k5;
        display.setContrast((uint8_t)(k5 * 255 / 100));
        changed = true;
    }

    if (changed) { drawScreenInfo2(); dirty = true; dirtyTime = now; }

    if (dirty && millis() - dirtyTime >= 2000) {
        saveSettings();
        dirty = false;
    }
}

void updateInfo3Controls() {
    static bool active  = false;
    static int  lastPos = -1;

    if (!inInfoMode || infoPage != 4) {
        active = false;
        return;
    }

    if (!active) {
        // UP decrements threeWayPosition, DOWN increments — invert to map UP→higher layout
        threeWayPosition = constrain(3 - pageLayout, 0, 2);
        lastPos = threeWayPosition;
        active  = true;
    }

    if (threeWayPosition != lastPos) {
        lastPos    = threeWayPosition;
        pageLayout = constrain(3 - threeWayPosition, 1, 3);
        boardSettings.layoutId = (uint8_t)pageLayout;
        saveSettings();
        drawScreenInfo3();
    }
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
                else                   { inInfoMode = false; if (currentMode == 2) drawScreenMode2(); else if (currentMode == 3) drawScreenMode3(); else drawScreen(currentScreen); }
            } else if (currentMode == 2) {
                const int perPage  = MODE2_COLS * 2;
                const int numPages = (EFFECT_COUNT + perPage - 1) / perPage;
                if (numPages > 1) {
                    mode2Page = (mode2Page + 1) % numPages;
                    drawScreenMode2();
                }
            } else {
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
                if      (currentMode == 2) drawScreenMode2();
                else if (currentMode == 3) drawScreenMode3();
                else                       drawScreen(currentScreen);
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
    if (hlSlot >= 0 && millis() > hlUntil && !isInInfoMode() && currentMode != 2 && currentMode != 3) {
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

// ── Mode 3: OTHER-section controls ────────────────────────────────────────────
// Knob 1 → input gain, knob 5 → output gain, footswitch A / effect switch →
// on/off, knob 3 + 3-way switch → whichever of mode3SwitchOptions[] is selected
// (Pre FX / Post FX / Cab / Doubler — defaults to Doubler).

void drawScreenMode3() {
    display.clearDisplay();

    // ── Knockout header ───────────────────────────────────────────────────────
    display.fillRect(0, 0, 128, 10, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 1);
    display.print("OTHER");
    display.setCursor(3, 1);
    display.print("OTHER");
    display.setTextColor(SH110X_WHITE);

    char buf[8];

    const Parameter& inGain = parameters[OTHER][OTHER_INPUT_GAIN];
    if (inGain.known) snprintf(buf, sizeof(buf), "%d", inGain.value);
    else              strcpy(buf, "?"); // real DAW-side value not learned yet
    drawParamRow(11, "I", inGain.label, buf);

    const Parameter& outGain = parameters[OTHER][OTHER_OUTPUT_GAIN];
    char buf2[8];
    if (outGain.known) snprintf(buf2, sizeof(buf2), "%d", outGain.value);
    else               strcpy(buf2, "?"); // real DAW-side value not learned yet
    drawParamRow(20, "V", outGain.label, buf2);

    char onOffBuf[4];
    drawParamRow(38, "A", parameters[OTHER][OTHER_ON_OFF].label,
                 switchValStr(OTHER, OTHER_ON_OFF, onOffBuf, sizeof(onOffBuf)));

    int  selParamIdx = mode3SwitchOptions[mode3SelectedParam];
    char selBuf[4];
    drawParamRow(56, ">", parameters[OTHER][selParamIdx].label,
                 switchValStr(OTHER, selParamIdx, selBuf, sizeof(selBuf)));

    display.display();
}

void updateMode3Controls() {
    if (currentMode != 3 || isInInfoMode()) return;
    static int lastSel = -1;

#ifdef KNOB3_IS_ENDLESS
    readKnob(3); // drives the atan2 update as a side effect → enc3LastDial is current
    int sel = constrain(getKnob3RawDial() * 4 / ENC3_DIAL_RANGE, 0, 3);
#else
    int sel = constrain(map(readKnob(3), 0, 4095, 3, 0), 0, 3);
#endif

    if (sel != lastSel) {
        mode3SelectedParam = sel;
        lastSel            = sel;
        drawScreenMode3();
    }
}

void updateMode2Controls() {
    if (currentMode != 2 || isInInfoMode()) return;
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
    pinMode(PIN_DISPLAY_SWITCH, INPUT_PULLUP);
    SPI.begin(OLED_SCK, -1, OLED_MOSI);
    if (!display.begin(0, true)) {
        DBG_PRINTLN("SH1106G not found");
        while (1);
    }
    display.setRotation(2); // 180° — adjust to 0 if screen is mounted the other way
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
    if      (currentMode == 2) drawScreenMode2();
    else if (currentMode == 3) drawScreenMode3();
    else if (isInInfoMode())   {} // updateInfo1Live() redraws within 1 s
    else                       drawScreen(currentScreen);
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
