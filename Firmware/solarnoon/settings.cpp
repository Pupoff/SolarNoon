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
