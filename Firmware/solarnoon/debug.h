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

// ── Serial debug output ───────────────────────────────────────────────────────
// Uncomment to enable all Serial.print* output across the project.
// Comment out for production builds (zero overhead, macros compile to nothing).
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
