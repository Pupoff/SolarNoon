#include "leds_rgb.h"
#include "debug.h"
#include "pin.h"
#include "midi.h"     // ledMidiEntries[], ledMidiEntryCount
#include "settings.h" // boardSettings

static CRGB leds[NUM_RGB_LEDS];       // FastLED working buffer (receives scaled copy)
static CRGB trueColors[NUM_RGB_LEDS]; // intended colors, unscaled

void initRGBLeds() {
    memset(trueColors, 0, sizeof(trueColors));
    FastLED.addLeds<WS2812B, PIN_RGB_DATA, GRB>(leds, NUM_RGB_LEDS);
    FastLED.setBrightness(255);
    FastLED.clear(true);
}

void updateLedMidiFeedback() {
    for (int i = 0; i < ledMidiEntryCount; i++) {
        const LedMidiEntry& e = ledMidiEntries[i];
        if (!e.valid || e.device.type != LED_DEVICE_RGB) continue;
        bool on = (e.value > 63);
        CRGB color = on ? CRGB(e.onColor.r, e.onColor.g, e.onColor.b)
                       : CRGB(e.onColor.r / 16, e.onColor.g / 16, e.onColor.b / 16);
        setRGBledRaw(e.device.id, color);
    }
}

static bool applyColor(int led, CRGB color) {
    if (led == RGB_RING) {
        for (int i = RGB_RING_START; i < RGB_RING_START + RGB_RING_SIZE; i++) trueColors[i] = color;
    } else if (led >= 0 && led < NUM_RGB_LEDS) {
        trueColors[led] = color;
    } else {
        DBG_PRINTLN("setRGBled: invalid LED identifier");
        return false;
    }
    return true;
}

void setRGBled(int led, CRGB color) {
    if (applyColor(led, color)) FastLED.show();
}

void setRGBledRaw(int led, CRGB color) {
    applyColor(led, color); // no show — caller must call showRGBLeds()
}

void showRGBLeds() {
    uint8_t switchScale = (uint8_t)(boardSettings.rgbSwitchBrightness * 255 / 100);
    uint8_t ringScale   = boardSettings.rgbRingEnabled
                          ? (uint8_t)(boardSettings.rgbRingBrightness * 255 / 100)
                          : 0;
    for (int i = 0; i < NUM_RGB_LEDS; i++) {
        leds[i] = trueColors[i];
        leds[i].nscale8(i < RGB_RING_START ? switchScale : ringScale);
    }
    FastLED.show();
}
