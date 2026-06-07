#include "pin.h"
#include "leds_mono.h"
#include "leds_rgb.h"
#include "display.h"
#include "knobs.h"
#include "midi.h"
#include "effects.h"
#include "three_way_switch.h"
#include "switches.h"
#include "startup.h"
#include "debug.h"
#include "settings.h"
#include "battery.h"



void setup() {
    Serial.begin(115200);
    loadSettings(); // must be before initDisplay() so contrast is applied at boot
    initMidi();
    initLedsMonochrome();
    initParameters(); // must run before initDisplay() so screen 1 has valid data
    buildMidiLookup(); // must run after initParameters() so midiNote fields are populated
    initMode();
    applyEffectColor();
    initDisplay();
    initRGBLeds();
    initStatusLeds();
    startupAnimation();
    initKnobs();
    initThreeWaySwitch();
    initSwitches();
    initBattery();

    pinMode(PIN_EFFECT_SWITCH, INPUT_PULLUP);

    for (int i = 1; i <= 5; i++) ledRingBehavior(i, SYNC_WITH_KNOB);

    displayRefreshNeeded = true;


// IS31 or GPIO — no color needed
//mapLEDtoMIDI(StatusLed(LED_WIFI),  {1, 70});
//mapLEDtoMIDI(UserLed(1),          {1, 60});

// RGB strip LED — with on-color
//mapLEDtoMIDI(RGBLed(LED_SWITCH_A), {1, 71}, {255, 0, 0});   // red when on
//mapLEDtoMIDI(RGBLed(LED_MODE),     {1, 72}, toRGBColor(getEffectColor())); // use current effect color

    setRGBled(LED_SWITCH_A, getEffectColor());
    setRGBled(LED_SWITCH_B, CRGB(160, 160, 160));
    setRGBled(LED_SWITCH_C, CRGB(160, 160, 160));
    setRGBled(LED_MODE, getModeColor());



}


void loop() {

     //debugPrintGPIO();
    // debugPrintKnobs();

    //MIDI update
    updateMidi(); // process incoming MIDI; sets displayRefreshNeeded if params changed

    //Screen Update
    if (displayRefreshNeeded && !isInInfoMode() && !isBleWarnActive()) {
        displayRefreshNeeded = false;
        if      (currentMode == 2) drawScreenMode2();
        else if (currentMode == 3) drawScreenMode3();
        else                       drawScreen(currentScreen);
    }

    //UI LED Update
    updateRings();
    if (!isInInfoMode()) {
        if (currentMode == 3) {
            setRGBledRaw(LED_EFFECT,   CRGB::Black);
            setRGBledRaw(LED_SWITCH_A, CRGB(255,   0,   0));
            setRGBledRaw(LED_SWITCH_B, CRGB(  0, 255,   0));
            setRGBledRaw(LED_SWITCH_C, CRGB(255, 255, 255));
        } else {
            updateEffectLeds();
            updateLedMidiFeedback();
            int  effIdx = (currentMode == 2) ? mode2SelectedEffect : currentEffectIdx;
            int  eff    = effectSections[effIdx];
            bool enabled = (bool)parameters[eff][0].value;
            CRGB col     = getEffectColorByIdx(effIdx);
            if (!enabled) col.nscale8(30);
            setRGBledRaw(LED_SWITCH_A, col);
            if (currentMode == 2) {
                CRGB effCol = getEffectColorByIdx(effIdx);
                if (!enabled) effCol.nscale8(20);
                setRGBledRaw(LED_EFFECT, effCol);
            }
            setRGBledRaw(LED_SWITCH_B, CRGB(160, 160, 160));
            setRGBledRaw(LED_SWITCH_C, CRGB(160, 160, 160));
        }
    }
    updateBatteryLeds();
    checkBatteryShutdown();
    {
        static bool lastBle = isBleMode(); // initialised to boot state on first call
        bool bleOn = digitalRead(PIN_BLE_SW) == HIGH;
        setIS31Led(LED_WIFI, bleOn ? 0 : 255); // LED_WIFI repurposed: lit when BLE is OFF
        if (bleOn && isBleMode() && !isMidiMounted()) {
            // Breathing animation while advertising but not connected — 2 s period
            float phase = (millis() % 2000) * (2.0f * PI / 2000.0f);
            setIS31Led(LED_BLE, (int)(5.0f + (1.0f - cosf(phase)) * 100.0f));
        } else {
            setIS31Led(LED_BLE, bleOn ? 255 : 0);
        }
        if (bleOn != lastBle) {
            lastBle = bleOn;
            if (isInInfoMode()) displayRefreshNeeded = true;
            if (bleOn != isBleMode()) {
                // Switch diverged from the boot-time transport selection
                if (!bleOn && isBleMode()) disableBle(); // stop sending while BLE hw runs
                triggerBleWarning(bleOn);
            }
        }
    }
    updateModeLed();
    updateRingByMode();
    updateThreeWaySwitch();
    updateInfo1Live();
    updateInfoBLELive();
    updateInfo2Controls();
    updateInfo3Controls();
    updateMode2Controls();
    updateMode3Controls();

    tickHighlight();
    updateBleWarning();

    //Handle switches
    updateSwitchesMidi();
    handleDisplaySwitch();
    handleEffectSwitch();

    showRGBLeds(); // after all SPI display ops to avoid RMT/SPI conflict
    delay(10);
} 
