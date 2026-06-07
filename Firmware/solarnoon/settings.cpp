#include "settings.h"
#include <Preferences.h>

BoardSettings boardSettings;

void loadSettings() {
    Preferences prefs;
    prefs.begin("a14h", true); // read-only namespace
    boardSettings.ringMonoBrightness  = prefs.getUChar("ringMono",  80);
    boardSettings.rgbRingBrightness   = prefs.getUChar("rgbRing",   60);
    boardSettings.rgbSwitchBrightness = prefs.getUChar("rgbSwitch", 80);
    boardSettings.displayContrast     = prefs.getUChar("dispContr", 80);
    boardSettings.rgbRingEnabled      = prefs.getBool ("rgbRingOn", true);
    boardSettings.layoutId            = prefs.getUChar("layoutId",  2);
    boardSettings.deviceMode          = prefs.getUChar("devMode",   1);
    boardSettings.enc3Sensitivity     = prefs.getUChar("enc3Sens",  80);
    prefs.end();
}

void saveSettings() {
    Preferences prefs;
    prefs.begin("a14h", false); // read-write
    prefs.putUChar("ringMono",  boardSettings.ringMonoBrightness);
    prefs.putUChar("rgbRing",   boardSettings.rgbRingBrightness);
    prefs.putUChar("rgbSwitch", boardSettings.rgbSwitchBrightness);
    prefs.putUChar("dispContr", boardSettings.displayContrast);
    prefs.putBool ("rgbRingOn", boardSettings.rgbRingEnabled);
    prefs.putUChar("layoutId",  boardSettings.layoutId);
    prefs.putUChar("devMode",   boardSettings.deviceMode);
    prefs.putUChar("enc3Sens",  boardSettings.enc3Sensitivity);
    prefs.end();
}
