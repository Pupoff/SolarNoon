#include "debug.h"
#include "pin.h"
#include <Arduino.h>

// ── helpers ───────────────────────────────────────────────────────────────────

static void printSwitch(const char* name, int pin) {
    int v = digitalRead(pin);
    DBG_PRINTF("  %-16s IO%-3d  switch   %s\n",
                  name, pin, v == LOW ? "LOW  (pressed)" : "HIGH (open)");
}

static void printAnalog(const char* name, int pin) {
    int v = analogRead(pin);
    int pct = v * 100 / 4095;
    DBG_PRINTF("  %-16s IO%-3d  analog   %4d  (%3d%%)\n",
                  name, pin, v, pct);
}

// ── public ────────────────────────────────────────────────────────────────────

void debugPrintGPIO() {
    DBG_PRINTLN("\n  Name             IO    Type     Value");
    DBG_PRINTLN("  ─────────────────────────────────────────────");

    DBG_PRINTLN("  [switches]");
    printSwitch("SW_A",        PIN_SW_A);
    printSwitch("SW_B",        PIN_SW_B);
    printSwitch("SW_C",        PIN_SW_C);
    printSwitch("EFFECT_SW",   PIN_EFFECT_SWITCH);
    printSwitch("DISPLAY_SW",  PIN_DISPLAY_SWITCH);
    printSwitch("3WAY_UP",     PIN_3WAY_UP);
    printSwitch("3WAY_DOWN",   PIN_3WAY_DOWN);

    DBG_PRINTLN("  [knobs]");
    printAnalog("K1",  KNOB_PINS[0]);
    printAnalog("K2",  KNOB_PINS[1]);
    printAnalog("K3",  KNOB_PINS[2]);
    printAnalog("K4",  KNOB_PINS[3]);
    printAnalog("K5",  KNOB_PINS[4]);

    DBG_PRINTLN("  ─────────────────────────────────────────────");
}
