#pragma once

// ── Serial debug output ───────────────────────────────────────────────────────
// Uncomment to enable all Serial.print* output across the project.
// Comment out for production builds (zero overhead — macros compile to nothing).
 #define SERIAL_DEBUG

#ifdef SERIAL_DEBUG
  #define DBG_PRINT(x)    Serial.print(x)
  #define DBG_PRINTLN(x)  Serial.println(x)
  #define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG_PRINT(x)    ((void)0)
  #define DBG_PRINTLN(x)  ((void)0)
  #define DBG_PRINTF(...) ((void)0)
#endif

void debugPrintGPIO();
