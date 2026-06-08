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
#include "parameters.h" // Parameter

// ── Generic helpers for self-contained modes ──────────────────────────────────
// A self-contained mode (see GETTING_STARTED.md, "Solar System" walkthrough)
// owns a physical control outright instead of plugging into the shared
// parameter/pickup machinery. These helpers cover the common case where what
// you actually want is to drive one of the *named* parameters in
// `effectControls[][]` (so the screen, MIDI feedback, and everything else that
// reads that `Parameter` stay in sync) without writing the
// read/map/compare/send/redraw boilerplate by hand each time.
//
// If you just want to read a knob or fire MIDI directly with no `Parameter`
// involved at all, see GETTING_STARTED.md "Reading a knob"/"Sending MIDI"
// instead, that's the lighter-weight, fully manual path. These helpers are for
// when you *do* want a `Parameter` (and the bookkeeping it gives you) but
// still want the mode to own the control directly (no soft takeover).
//
// All of them follow the same shape: give them the `Parameter&` (and whatever
// physical control drives it), they do the read/map/compare/send/redraw dance
// for you and leave `p.value`/`p.known` correctly updated.
// See GETTING_STARTED.md, "Driving `effectControls[][]` directly" for the
// full explanation (including why `effectControls[][]` exists at all).

// Debounced press detector for a momentary footswitch: returns true exactly
// once, the tick a press (pin reads LOW) is detected and has stayed stable for
// 50ms. The caller owns the state — declare one ButtonEdge per physical switch
// you track — and decides what a press should do; this just tells you "now".
struct ButtonEdge {
    bool          lastReading = HIGH;
    bool          lastStable  = HIGH;
    unsigned long lastChange  = 0;
};
bool buttonPressed(int pin, ButtonEdge& state);

// Drives a parameter directly from a knob: reads it raw, maps it to 0-127,
// and — only when it actually changed — writes it into `p` (scaled to the
// usual 0-100 range), sends the CC, mirrors the position on its own ring, and
// flags a redraw. The "direct mapping, send on change" pattern
// GETTING_STARTED.md describes for self-contained modes (no soft takeover, see
// "KNOB_MODE": turning the knob can audibly jump the sound the first time).
void driveKnobFromControl(int knobNum, Parameter& p);

// Flips a momentary on/off parameter (footswitch-style toggle): give it the
// parameter, it flips .value, sends the toggle, and asks for a redraw.
void driveToggleFromControl(Parameter& p);

// Drives a momentary on/off parameter directly from a footswitch: detects a
// debounced press on `pin` (see buttonPressed()/ButtonEdge) and, on press,
// hands it to driveToggleFromControl() — give it the pin, the edge state, and
// the parameter, the press-and-toggle is handled.
void driveSwitchFromControl(int pin, ButtonEdge& state, Parameter& p);

// Drives a plain on/off parameter directly from the 3-way switch (BOT/UP =
// off/on, MID unused — same 2-state convention updateSwitchMidi() uses): when
// `resync` is true (just selected/entered), it only re-displays `p`'s stored
// value, without sending anything; otherwise it sends on physical movement via
// sendMidiSwitch(), same ABSOLUTE-vs-TOGGLE plugin-mapping ambiguity as any
// other 2-state switch. Only one mode drives the switch this way at a time, so
// a single shared "last position" is enough.
void drive3waySwitchFromControl(Parameter& p, bool resync);
