# Getting started: customizing the firmware


## Table of contents

- [Generic changes](#generic-changes)
- [Display modes](#display-modes)
- [Manual reading/writing](#manual-readingwriting)
- [Manual example: "Solar System" walkthrough (mode 4)](#manual-example-solar-system-walkthrough-mode-4)
- [Automatic mapping with `effectControls[][]` structure](#automatic-mapping-with-effectcontrols-structure)
- [Automatic mapping example: Mode 3 ("archetypeVSTglobalcontrols")](#automatic-mapping-example-mode-3-archetypevstglobalcontrols)

This is a getting-started guide for customizing the firmware: the building
blocks you have available, and examples that ties them all together.

Except for some `#define` where the proper file is specified,  all the customization points described here live in `midi_a14h.ino`: `setup()` (one-time wiring) and `updateModeIO()` / `drawModeScreen()` (per-mode,
per-tick behavior).

[⬆ Back to top](#table-of-contents)

## Generic changes

This part presents some behavior changes you can make to change the way the firmware works, without having
to write your own code.

### Layout/timing constants you can tweak

A few plain numeric constants control layout and timing without any other code change:

- **`MODE2_COLS`** (`display.cpp`, default `2`): number of columns in mode 2's
  effect grid. Pages show `MODE2_COLS * 2` effects (2 rows), and box width is
  derived from it, so changing it reflows the whole grid automatically.
- **`LONG_PRESS_MS`** (default `600`): how long, in milliseconds, a switch
  must be held to count as a long press rather than a tap. It's `#define`d
  twice, once in `effects.cpp` (effect switch: long-press cycles the display
  mode) and once in `display.cpp` (display switch: long-press opens the info
  pages / pages through mode 2's effect grid). They're independent, change
  either or both to taste.
- **`HIGHLIGHT_MS`** (`display.cpp`, default `3000`): how long, in
  milliseconds, a parameter row stays highlighted on the parameter-view screen
  after you change it with a knob (`notifyParamChanged()` sets the deadline,
  `tickHighlight()` clears it once it passes).

### `KNOB_MODE`: what happens when the knob and the parameter disagree

Whenever a physical knob's position doesn't match the parameter it's about to
control (you just changed page/effect, or the DAW changed the value via MIDI
feedback while you weren't touching the knob), `updateKnobsMidi()` has to
decide what to do about that mismatch. `KNOB_MODE` (`parameters.h`) picks the
policy, for every knob, everywhere in the firmware:

```cpp
#define KNOB_MODE_JUMP   0
#define KNOB_MODE_PICKUP 1
#define KNOB_MODE KNOB_MODE_PICKUP
```

- **`KNOB_MODE_JUMP`**: the parameter snaps to the knob's physical position
  the instant it moves, even if that's far from the current value. Immediate,
  but can cause an audible jump in the sound.
- **`KNOB_MODE_PICKUP`**: the knob is ignored (soft takeover) until it
  physically crosses or lands on the parameter's current value, then it takes
  over smoothly from there. No jump, but "dead" until it catches up.

This only matters if you're driving `effectControls[][].value` through the
normal pickup machinery in `updateKnobsMidi()`. A mode that owns its knobs
directly (like mode 4, reading `readKnob()` and sending fixed CCs itself) is
unaffected: there's no "parameter" to disagree with, so nothing to pick up.

[⬆ Back to top](#table-of-contents)

## Display modes

The screen has a fixed number of "modes": full-screen views you cycle
through by long-pressing the effect footswitch. The first 2 modes (parameter view and effect grid) are hardcoded in the firmware and not meant to be modified. (You can modify them, but
there is no easy API to do it.)
If you want to remove one of them, you can simply set `modeEnabled[N] = false;` in the setup,
which defines which displays are active.

A few empty modes are already in place, but if you want more, you will need to add a new mode:

1. Bump `#define MODE_COUNT` in `effects.h`.
2. Set `modeEnabled[N] = true;` in `setup()`.
3. Write `void drawScreenModeN()` (what the screen looks like) and add a
   `case N:` for it in `drawModeScreen()`.
4. Add a `case N:` to `updateModeIO()` for whatever the LEDs/rings/footswitches
   should do while that mode is active (optional: the `default:` case covers
   "nothing yet").

`drawModeScreen()` and `updateModeIO()` are companion dispatchers, both keyed
on the mode number: one decides what the **screen** shows, the other what the
**LEDs/switches/MIDI** do. Keeping their `case` lists in the same order makes
it easy to compare what a given mode does on both fronts.

[⬆ Back to top](#table-of-contents)

## Manual reading/writing

This section covers how to read a knob, send MIDI, or drive an LED fully
manually: you own all the state yourself, and nothing else in the firmware
knows or cares what you're doing.

 If you'd rather have the firmware
handle the screen display, MIDI feedback, mode 1 integration and the "?" shown for an
unknown value...  see "effectControls structure" further down. That's what `effectControls.h` and mode 1 already
use, and what `direct_controls.h`'s helpers give a self-contained mode access
to directly.

### Reading a knob

```cpp
int raw = readKnob(knobID); // knobID 1–5, returns the raw ADC position 0–4095
```

Call it directly whenever you want a knob's live position: for drawing, for
sending MIDI, for anything. It's debounced/oversampled internally, so you
don't need to worry about ADC noise. `map(raw, 0, 4095, loVal, hiVal)` is the
usual way to rescale it (e.g. to 0–100 for display, 0–127 for MIDI).

Note that `KNOB_MODE_JUMP` doesn't affect this value.

### Sending MIDI

```cpp
sendMidiCC(ccNumber, value); // value 0–127, sent on MIDI_CHANNEL_SEND (parameters.h)
```

The simplest way to send a CC. Only call it when the value actually changed:
sending on every tick floods the MIDI output for no benefit (see the Solar
System walkthrough for the standard "send on change" pattern).

### Reading the 3 way switch

```cpp
int pos = readThreeWaySwitch(); // 0 = up, 1 = middle, 2 = bottom
```

The `readKnob()`-style getter for it (`three_way_switch.h`): the current
debounced position, kept current once per tick by `updateThreeWaySwitch()`
(called generically from `loop()`, regardless of mode.
The same way `readKnob()` hides the raw ADC/hysteresis logic, this hides the raw-pin/debounce
logic behind a plain getter). 
Prefer it over reading the underlying `threeWayPosition` global directly.

### Writing the LED rings

Each knob has its own 16-LED monochrome ring that you can drive directly with a value of your choosing.
```cpp
ringMidiValues[k] = value;
```

Write whatever value (0–255) you want it to show into `ringMidiValues[k]`, every tick, from your
`updateModeIO()` case.
If you don't want to show anything, don't forget to zero all five so they don't display stale data left over from whichever mode ran before yours.


### Attribute footswitches to a midi CC: `switchCC[]`

```cpp
extern uint8_t switchCC[3]; // [0]=switch A, [1]=switch B, [2]=switch C
```

Each footswitch (switch A, B and C) sends `switchCC[i]` as a momentary 127 (press) / 0 (release)
pulse. Defaults are set once in `setup()` (`CC_SW_A/B/C`, see `switches.h`),
but it can be overwritten per mode in
`updateModeIO()`'s switch (ex: `switchCC[1] = 80;` to give
switch B a different meaning on that page only).

#### Switch combo: change mode by holding A and tapping B/C

```cpp
#define SWITCH_COMBO_MODE_CHANGE // switches.h, comment out to disable
```

When this define is present, holding switch A down and tapping B or C cycles
`currentMode` forward (B) or backward (C) through the enabled slots, the same
way long-pressing the effect footswitch does.
Comment the define out to go back to three plain, independent switches, if you want to use
long press or switch combo for something else.

### Direct LED control

```cpp
setRGBledRaw(ledID, CRGB(r, g, b));   // WS2812B strip LEDs, color
setStatusLed(index, brightness);      // IS31/GPIO single-color LEDs, brightness 0–255
```

`ledID`/`index` are just integers, but you don't need to remember the numbers,
named constants exist for every physical LED:

```cpp
// setRGBledRaw(): WS2812B strip LEDs (leds_rgb.h)
LED_SWITCH_A, LED_SWITCH_B, LED_SWITCH_C // footswitch indicators
LED_EFFECT                               // "effect" indicator
LED_MODE                                 // "display" indicator
RGB_RING_START, RGB_RING_SIZE            // the 24-LED ring around the display: indices
                                         // RGB_RING_START .. RGB_RING_START + RGB_RING_SIZE - 1
RGB_RING                                 // sentinel id: pass it instead of an index to set all 24 ring LEDs at once

// setStatusLed(): IS31/GPIO single-color LEDs (leds_mono.h)
LED_USER1, LED_USER2                     // The two LED between the footswitches

// Other that you shouldn't use directly:
LED_WIFI, LED_BLE                        // connection status
LED_BATTERY_1, LED_BATTERY_2, LED_BATTERY_3 // battery gauge bar
LED_3WAY_UP, LED_3WAY_MID, LED_3WAY_BOT  // 3-way switch position indicators
```

Use these whenever an LED's meaning depends on the mode (`currentMode`) or other live
state: read whatever you care about (`effectControls[][]`, knob position,
selection index…) and push the result yourself, every tick, from
`updateModeIO()`. This is how every existing mode paints its LEDs; see any
`case` in `updateModeIO()` for examples of recoloring `LED_MODE`/`LED_SWITCH_*`
and rebinding `LED_USER1`/`LED_USER2` per mode. 

If you want a permanent MIDI binding, see `mapLEDtoMIDI`

```cpp
    setRGBledRaw(LED_SWITCH_C, CRGB(160, 160, 160));
    setStatusLed(LED_USER1, doublerOn ? 255 : 0);
 ```              
To address every LED of the ring at once, loop over it:

```cpp
for (int i = 0; i < RGB_RING_SIZE; i++)
    setRGBledRaw(RGB_RING_START + i, CRGB(0, 60, 255)); // paint the whole ring blue
```


#### `effectLedColor`: the cached "current effect" color

`updateEffectLedColor()` (called every tick, from `updateUIled()`) figures out
the color of the currently selected effect: its section color at full
brightness when the effect is on, dimmed to about 8% brightness
(`col.nscale8(20)`, same hue) when it's off, and caches the result in
`effectLedColor` so you don't have to recompute that on/off/color logic
yourself. Mode 1 reuses it directly: `setRGBledRaw(LED_SWITCH_A,
effectLedColor)` paints the footswitch the same color as the active effect.

`setRGBledRaw()` only stages the color, `showRGBLeds()` (called once at the
end of `loop()`) actually pushes it to the strip, so you can batch many calls
per tick cheaply.


### Writing the 3 way switch position

You can't *move* the physical switch, of course, but you can drive its three
position-indicator LEDs (the ones right next to it) directly:

```cpp
setThreeWaySwitchLeds(pos); // 0=up, 1=mid, 2=bottom; any other value (e.g. -1) blanks all three
```

This is what `updateThreeWaySwitch()` itself already uses internally to keep those
LEDs tracking `threeWayPosition`. You shouldn't really use this as the LED
are the only way to see the position of the switch (not like the LED rings
that can be independant of the knobs position).

Careful with `updateThreeWaySwitch()` conflicts. 


### Automatically send a CC and change the LED ring with a knob

Two small per-tick helpers package up the "read a control, map it to a fixed
CC, send only on change, keep its LED ring or position-LEDs in sync" pattern
that mode 4 (and the Solar System walkthrough below) uses for both its knobs
and its 3-way switch. Call one of these whenever a control should drive one fixed CC and nothing fancier
(pickup/takeover, `effectControls[][]`…) is needed:

```cpp
driveKnobToCC(int knobNum, int ringIdx, uint8_t cc, int& lastSent);
drive3waySwitchToCC(uint8_t cc, int& lastSent);
```

`driveKnobToCC` (`knobs.h`/`.cpp`) reads knob `knobNum` (1-5), maps it to
0-127, mirrors that value onto LED ring `ringIdx`, and sends `cc` only when it
changes (tracked in `lastSent`, a `static int` you own). `drive3waySwitchToCC`
(`three_way_switch.h`/`.cpp`) is the same shape for the 3-way switch: it maps
the switch's three positions to three fixed CC values (0/64/127) and sends
`cc` only on change. Both flag `displayRefreshNeeded` when they send, so
anything your mode draws from that control's value redraws right when it
moves — see "5. 3-way switch → its own fixed CC" below for `drive3waySwitchToCC`
in actual use.



### One-shot LED-to-MIDI binding: `mapLEDtoMIDI()`

For an LED that should *permanently* reflect an incoming MIDI CC (e.g. "light
up green when the DAW turns delay on"), wire it once in `setup()`:

```cpp
// IS31 / GPIO / UserLed, no color needed
mapLEDtoMIDI(StatusLed(LED_WIFI), {1, 70});       // {channel, ccNumber}
mapLEDtoMIDI(UserLed(1),          {1, 60});

// RGB strip LED, give it an "on" color (full bright when CC > 63, dim otherwise)
mapLEDtoMIDI(RGBLed(LED_SWITCH_A), {1, 71}, {255, 0, 0});
mapLEDtoMIDI(RGBLed(LED_MODE),     {1, 72}, toRGBColor(getEffectColor()));
```
This binding is fixed at boot: it can't change with the mode. If an LED's
MIDI mapping (or meaning) needs to depend on which mode/page you're on, don't
use `mapLEDtoMIDI`: drive it yourself each tick from `updateModeIO()` (see
"Direct LED control" above), reading the live value as described in "Reading
MIDI feedback yourself" below.

You can do the same for LED rings; unlike other LEDs, rings *can* safely be
rebound on the fly (e.g. once per mode/page, on the `loop()`): each call simply overwrites
that ring's binding in place:

```cpp
// Rings, same function, can safely be rebound on the fly (e.g. per page)
mapLEDtoMIDI(RingLED(3), {1, 74});
```

*Note:* for any other LED type, never call `mapLEDtoMIDI` from the loop. Every
non-ring LED is appended to a small fixed-size table with no update/remove:
call it once, in `setup()`. Calling it repeatedly (e.g. every tick) just
appends duplicate entries until the table fills up.

### Reading MIDI feedback yourself

There's no "give me the live value of arbitrary CC X" API: incoming feedback
is parsed once, centrally, in `channelMessageCallback()` (`midi.cpp`), and
dispatched from there to whatever's registered for that CC: `effectControls[][]`,
`ringMidiMap[]`, or `mapLEDtoMIDI()`'s table.

The one piece of that you *can* read directly yourself is
`effectControls[section][param]`: its `.value`/`.known` are updated
automatically the instant feedback for its CC arrives, no extra wiring
needed, just read them. So a mode-dependent LED that mirrors a named
parameter is just:

```cpp
if (effectControls[AMP][0].known)
    setStatusLed(LED_USER1, effectControls[AMP][0].value * 255 / 100);
else
    setStatusLed(LED_USER1, 0); // "?" — no feedback received yet
```

If the CC you care about isn't a named parameter yet, give it an entry in
`effectControls[][]` (see "Automatic mapping" below) so the central dispatch
starts tracking it for you.

---

[⬆ Back to top](#table-of-contents)

## Manual example: "Solar System" walkthrough (mode 4)

Mode 4 ("Solar System") is a complete worked example of adding a new display
mode from scratch: screen, LED ring, RGB LEDs, footswitch, and MIDI all
wired up. It's meant to be read end to end as a template for your own modes.
The code lives in `midi_a14h.ino`: `drawScreenMode4()` and `case 4:` in
`updateModeIO()`.

### What it does

Five concentric orbits, one per knob (knob 1 = innermost). Turning a knob
moves its "planet" around its orbit and sends a MIDI CC; the knob's LED ring
lights up proportionally to the same position. The screen also folds in a third
physical control: a small star that moves to one of three fixed spots along the
right edge depending on the 3-way switch's position.

### The screen: `drawScreenMode4()`

First, pick a free slot in `setup()` and enable it:

```cpp
modeEnabled[4] = true; // enable slot 4 for Solar System
```

Then wire up your draw function in `drawModeScreen()`'s switch:

```cpp
case 4: drawScreenMode4(); break;
```

If no slot is free, see "Display modes" above.

Now write `drawScreenMode4()`. It draws three things.

**Header** (filled rectangle with inverted text):

```cpp
display.clearDisplay();
display.fillRect(0, 0, 128, 10, SH110X_WHITE);
display.setTextColor(SH110X_BLACK);
display.setCursor(2, 1);
display.print("SOLAR SYSTEM");
display.setTextColor(SH110X_WHITE);
```

**Sun + orbits + planets** (one orbit and dot per knob):

```cpp
const int cx = 64, cy = 37;
display.fillCircle(cx, cy, 2, SH110X_WHITE); // the "sun"

for (int k = 0; k < 5; k++) {
    int radius = 5 + k * 5; // 5,10,15,20,25: fits between header and bottom edge
    display.drawCircle(cx, cy, radius, SH110X_WHITE);

    int   value = map(readKnob(k + 1), 0, 4095, 0, 100);
    float angle = (value / 100.0f) * 2.0f * PI + (float)PI / 2.0f;
    int   px    = cx + (int)(radius * cosf(angle));
    int   py    = cy + (int)(radius * sinf(angle));
    display.fillCircle(px, py, 2, SH110X_WHITE);
}
```

**Star** (crosshair at one of three fixed vertical positions):

```cpp
static const int starY[3] = {15, 37, 59}; // up, mid, bottom
int sx = 116, sy = starY[readThreeWaySwitch()];
display.drawLine(sx - 3, sy,     sx + 3, sy,     SH110X_WHITE);
display.drawLine(sx,     sy - 3, sx,     sy + 3, SH110X_WHITE);
```

Then commit:

```cpp
display.display();
```

The display API is Adafruit GFX / `Adafruit_SH110X` — see its documentation for
the full set of drawing primitives.

*Note on `readKnob()` inside a draw function:* `drawModeScreen()` is only called
when `displayRefreshNeeded` is true, so reading a knob here is fine — the
per-tick code (below) sets that flag whenever a knob moves enough to matter,
which is what forces the redraw. Just be aware that the screen only updates
*because* the tick code raises the flag, not because `drawScreenMode4()` polls
the knob itself.

### The per-tick behavior: `case 4:` in `updateModeIO()`

Called every tick (~10 ms), this is where every "generic building block" above gets exercised together:

**1. RGB LEDs**: paint the mode controls a consistent "sun" gold, the same
way every other mode recolors `LED_MODE`/`LED_SWITCH_*` for its own theme:

```cpp
setRGBledRaw(LED_MODE,     CRGB(255, 200, 0));
setRGBledRaw(LED_SWITCH_A, CRGB(255, 200, 0));
```
The LED effect is turned off, as well as the ring:

```cpp
setRGBledRaw(LED_EFFECT,     CRGB(0, 0, 0));
setRGBledRaw(RGB_RING,       CRGB(0, 0, 0));

```


**2. Footswitch remap**: switch B sends a different CC on this page only
(see "Footswitches" above for the general mechanism):

```cpp
switchCC[1] = 95;
```

**3. Knob → MIDI**, sent only when the value actually changes (so turning a
knob doesn't flood the MIDI output every 10 ms):

```cpp
static const uint8_t solarCC[5]  = {90, 91, 92, 93, 94}; // picked to sit above
static int           lastSent[5] = {-1, -1, -1, -1, -1}; // every effectControls CC (see effectControls.h)
...
int value = map(readKnob(k + 1), 0, 4095, 0, 127);
if (value != lastSent[k]) {
    lastSent[k] = value;
    sendMidiCC(solarCC[k], value);
    displayRefreshNeeded = true; // tell drawModeScreen() to redraw the moved planet
}
```

Setting `displayRefreshNeeded` here is what makes the planet redraw when the
knob moves. If a mode had no MIDI to send but still needed a redraw on change,
it would use the same `if (value != lastSent[k])` guard and set the flag
there, without the `sendMidiCC` call.

**4. Knob → LED ring**, mirroring the very same position the MIDI CC carries:
this is the `SYNC_WITH_MIDI_CUSTOM` pattern from "LED rings" above. Mode 4
owns these rings (it's not mode 1), so nothing else will write
`ringMidiValues[]` for it; we write it ourselves, every tick, scaled from
MIDI's 0–127 range to the ring's 0–255 brightness range:

```cpp
ringMidiValues[k] = value * 255 / 127;
```

Because Knob → MIDI + Knob → ring is such a common pattern, steps 3 and 4
together collapse into a single helper call:

```cpp
driveKnobToCC(k + 1, k, solarCC[k], lastSent[k]);
```

This does exactly the above: reads the knob, scales to 0–127, drives the ring,
sends MIDI on change, and sets `displayRefreshNeeded`.

**5. 3-way switch → its own fixed CC**: same "send on change" shape as the
knobs above (a `static lastSwitchSent` standing in for `lastSent[k]`), just a
3-state input instead of a continuous one (three fixed values, one per
position, picked from a small lookup table):

```cpp
static const uint8_t switchValues[3] = {0, 64, 127};
int switchValue = switchValues[readThreeWaySwitch()];
if (switchValue != lastSwitchSent) {
    lastSwitchSent       = switchValue;
    sendMidiCC(96, switchValue);
    displayRefreshNeeded = true; // tell drawScreenMode4() to redraw the moved star
}
```
Just like the knob loop above, this collapses into a single helper call —
`drive3waySwitchToCC()` (see "Automatically send a CC…" above):
```cpp
static int lastSwitchSent = -1;
...
drive3waySwitchToCC(96, lastSwitchSent);
```


That one line reads `readThreeWaySwitch()`, maps position 0/1/2 to CC value
0/64/127, sends CC 96 only when it changes, and sets `displayRefreshNeeded`,
 which is what makes the star in `drawScreenMode4()` actually jump to its
new spot the instant the switch moves.

Note what *isn't* here: no call to `setThreeWaySwitchLeds()`. Reading the
switch is always free, and so is showing its position on its own LEDs: that's
still handled for you by the generic `updateThreeWaySwitch()`, which already
knows mode 4 reads the switch as a plain 3-state position selector and lights
`LED_3WAY_UP/MID/BOT` to match, the same as it does for modes 1 and 3 (see
"Writing the 3 way switch position" above). The code above only needs
to draw the star and handle the midi; the LEDs take care of themselves.

[⬆ Back to top](#table-of-contents)

## Automatic mapping with `effectControls[][]` structure

The previous manual editing works if you just want to tweak a few midi controls, but for more ambitious projects, you'll need to use
the `effectControls` structure.

Let's say you want to bind a specific control to a knob. You could do it
fully manually, the same way the Solar System walkthrough does:

```cpp
int value = map(readKnob(k + 1), 0, 4095, 0, 127);
ringMidiValues[k] = value * 255 / 127;
if (value != lastSent[k]) {
    lastSent[k] = value;
    sendMidiCC(solarCC[k], value);
    displayRefreshNeeded = true;
}
```


But what happens if the DAW changes that same parameter from its own plugin
UI? With the code above you'd have no way to know, the value lives only in
`lastSent[k]`, private to this one block. To support MIDI feedback you'd need
to store the value somewhere shared, so that when feedback for that CC comes
in, you can update it *and* refresh whatever's currently showing it (screen,
LED ring, an LED…).

That shared storage is exactly what `effectControls[][]` is for. Each control
you might want to show or drive there isn't just a bare number, it's a
`Parameter` (`parameters.h`):

```cpp
struct Parameter {
    int         value;     // current value, the single source of truth for it
    int         midiNote;  // CC number this parameter is sent/received on
    ParamType   type;      // KNOB, SWITCH, THREE_WAY_SWITCH, SLIDER
    const char* label;     // shown on screen
    const char* const* labels; // option labels, THREE_WAY_SWITCH only
    bool        known;     // false until we've learned the real DAW-side value
};
```

Why a whole structure for this, rather than just a shared variable?

First, because `effectControls[section][param]` groups controls by *section*
(AMP, DELAY, OVERDRIVE…), the same way a classic control surface groups
controls into pages. Each section holds however many `Parameter`s it needs
(AMP, for instance, has gain, bass, mid, treble, output…), and that grouping
is what lets mode 1 page through them generically — picking up each section's
color, parameter count and labels on its own — and lets mode 2's effect grid
detect and display each section as a whole, all without a single line of
per-effect code.

Secondly, the screen, the MIDI-feedback handler, and any other mode that might
reference the same control all read `effectControls[][]` to know what's
currently true: a `Parameter` is the one shared place that answer lives:

- **The label and `midiNote` are already there.** `effectControls.h` is where
  every control's name and CC number are declared once; if you drove this
  control manually instead, you'd have to repeat that pairing yourself
  (and keep it in sync if it ever changes there).
- **`.known` drives the "?" on screen.** Until the firmware has learned the
  real DAW-side value (from feedback or its own first send), showing a number
  would be a lie, the screen shows "?" instead. Forget to set it and your
  control's row would claim a value it hasn't actually confirmed.
- **`.value` is what feedback updates.** If the DAW reports this CC changing
  (someone moved it from the plugin's own UI), `updateMidi()` writes the new
  value straight into `effectControls[][]` — any code that reads the
  `Parameter` (rather than keeping its own private copy) sees the up-to-date
  state for free, including your screen the next time it redraws.

In other words, this structure does the whole control-surface bookkeeping for
you, and gives mode 1 (which already knows how to bind knobs/switches to the
physical layout) instant access to every control declared this way.



### `effectControls[][]` architecture and how to write your own

`effectControls.h` is the one file that describes *what exists*: every
section (AMP, DELAY…), every parameter inside it, and how each parameter
should look and behave. Nothing in here talks to MIDI or the screen directly —
it's pure data, consumed by the generic machinery (mode 1, knob/switch
plumbing, `effects.cpp`'s LED-color logic…) described elsewhere in this guide.

Three pieces work together:

- **`enum Section`**: one identifier per section (`AMP`, `DELAY`, `OTHER`…),
  used as the first index into `effectControls[section][param]` everywhere.
  `SECTION_COUNT` is the trailing sentinel value, so it "automatically stays
  correct if sections are added/removed" — same trick as `EFFECT_COUNT = OTHER`
  just below it (see "Automatic mapping…" above for what that split means:
  sections before `OTHER` cycle through the effect grid, `OTHER`/`EQ` are
  reached directly by their own self-contained modes).

- **`sectionColors[SECTION_COUNT]`**: one `{r, g, b}` per section, *in enum
  order* — `sectionColors[AMP]` is red, `sectionColors[DELAY]` is green, etc.
  This is what `LED_EFFECT` (and anything that mirrors it, e.g.
  `LED_SWITCH_A`) lights up with whenever that section is the current one —
  see `getEffectColor()`/`updateEffectLedColor()` in `effects.cpp`.

- **`initializeEffectControlsTable()`**: one big function that fills
  `effectControls[][]` and `effectControlCount[]`, section by section. This is
  the part you'll actually edit — to change a label, MIDI note, knob range, or
  to add/remove a parameter, find the section's block and change the line.

Each line is built with one of the **parameter factories** from
`parameters.h`:

```cpp
effectControls[AMP][0] = Switch(0,  "AMP");
effectControls[AMP][1] = Knob  (1,  "Gain");
effectControls[AMP][8] = ThreeWay(53, "Amp", ampLabels);
```

`Knob`/`Slider`/`Switch`/`ThreeWay` all take a MIDI note number and a display
label, and return a `Parameter` (the struct everything else reads — value,
`midiNote`, `type`, `label`, `known`…). `ThreeWay` additionally takes an
options array — a `static const char* const xxxLabels[N]` declared just above
the block that uses it (e.g. `ampLabels`, `delayModeLabels`): the three
strings shown on screen for that switch's three positions (see
`effectControls[AMP][8].labels` / `Parameter::labels` in `parameters.h`).
Declaring it `static` keeps it local to `initializeEffectControlsTable()`
without re-allocating it every call; `const char* const` because neither the
pointers nor the strings they point to ever change.

**Index 0 of every section is special**: by convention it's that section's
on/off `Switch`, and its label *is* the section's display name —
`effectControls[AMP][0] = Switch(0, "AMP")`. That's not enforced by the
compiler, just a convention every section in the table follows, and the one
the generated `effectSections[]`/`effectNames[]` arrays (see "Automatic
mapping…" above) lean on: `effectNames[k] = effectControls[k][0].label`.
Break it for a section that's still in the cycle (before `OTHER`) and that
section's name in the mode-2 grid silently goes wrong.

Finally, `effectControlCount[section]` tells the generic paging code
(`getKnobParamIdx()`/`getSwitchParamIdx()`/`getPageCount()`, `effects.cpp`)
how many *pageable* parameters follow index 0 — set it once per section, right
after its block. `MAX_PARAMS` (in `parameters.h`) is just the fixed array
width that makes `effectControls[section][param]` a plain 2D array; sections
can (and do, see AMP's amp-type-dependent rows at indices 11/21) place extra
parameters past `effectControlCount` that the generic pager skips but specific
code (the AMP screen, in this case) reaches directly by index.


### Driving `effectControls[][]` directly: `direct_controls.h`

You *could* point `driveKnobToCC()` at an `effectControls[][]` cell's CC
number, but it knows nothing about `Parameter` — you'd still have to update
`p.value`/`p.known` (and everything that depends on them: the on-screen
feedback, the "?" handling, mode 1 staying in sync) by hand, every time. The
helpers below do that whole dance for you: give them the `Parameter&` and they
keep it correctly in sync as a side effect of driving it.
### The helpers (`direct_controls.h`/`.cpp`)

All of them follow the same shape: give them the `Parameter&` (and whatever
physical control drives it), they handle the read/map/compare/send/redraw
dance and leave `p.value`/`p.known` correctly updated. See
`archetypeVSTglobalcontrols` (`case 3` in `updateModeIO()`, `midi_a14h.ino`)
for them all in action together.

```cpp
// Knob → parameter, direct mapping, "send on change": one call replaces the
// read/map/compare/write/send/ring/redraw block the Solar System walkthrough
// shows you how to write by hand.
driveKnobFromControl(knobNum, effectControls[OTHER][0] /* OTHER_INPUT_GAIN */);

// Momentary on/off parameter (footswitch-style toggle): flips .value, sends
// the toggle (sendMidiSwitch(), see SWITCH_MIDI_MODE), redraws.
driveToggleFromControl(effectControls[OTHER][4] /* OTHER_ON_OFF */);

// Same, but reading + debouncing the footswitch itself: give it the pin, a
// ButtonEdge to remember its state in, and the parameter — the
// press-detection and the toggle are both handled.
static ButtonEdge swA;
driveSwitchFromControl(PIN_SW_A, swA, effectControls[OTHER][4] /* OTHER_ON_OFF */);

// Plain on/off parameter mapped onto the physical 3-way switch (BOT/UP =
// off/on, MID unused): `resync = true` re-displays the parameter's stored
// value without sending anything (use this right after the mode is entered,
// or the moment you remap the switch to a different parameter); otherwise it
// sends on physical movement.
drive3waySwitchFromControl(effectControls[OTHER][5] /* PRE_FX_SECTION */, modeJustEntered);
```

`buttonPressed(pin, ButtonEdge& state)`  (the debounced press-edge detector
`driveSwitchFromControl()` is built on) is also exposed on its own: declare
one `ButtonEdge` per physical switch you track, and it returns `true` exactly
once, the tick a press is detected and has stayed stable for 50ms. Useful any
time you want to react to a press but the action isn't "toggle a `Parameter`"
(e.g. cycling through a list of options, like knob 3 does in
`archetypeVSTglobalcontrols`).

### Claiming the effect switch: `modeEffectSwitchAction`

The helpers above cover knobs, footswitches and the 3-way switch, but a
self-contained mode might also want the *effect* switch's short press to do
something of its own — and that one's trickier, because it's shared with the
mode-cycling long-press every mode needs to keep working.

`handleEffectSwitch()` (`effects.cpp`) already hardcodes a short-press meaning
for modes 1 and 2 (cycle/toggle the current effect). For every other mode,
`modeEffectSwitchAction` (`extern void (*modeEffectSwitchAction)();`,
`effects.h`) is the generic claim mechanism, the `modeEffectSwitchAction`
idea applied to the *other* footswitch from `modeOwnsFootswitchA`
(`switches.h`, see "Footswitches" above): a function pointer, `nullptr` by
default (nothing happens on short press, e.g. mode 4 "Solar System"),
`updateModeIO()` resets it to `nullptr` every tick before dispatching to the
active mode's `case`, so a mode can't leave it claimed after switching away —
a self-contained mode just points it at its own handler every tick it wants
the press routed to it:

```cpp
modeEffectSwitchAction = []() { driveToggleFromControl(effectControls[OTHER][4] /* OTHER_ON_OFF */); };
```

A captureless lambda, not a named wrapper function: everything it references
(`effectControls`, `OTHER`) are globals/enum constants, so
there's nothing to capture and it converts implicitly to the plain
`void(*)()` that `modeEffectSwitchAction` is typed as (a lambda that needed
to close over a local variable couldn't do this, that requires
`std::function`, heavier and best avoided here — and a direct function *call*
like `driveToggleFromControl(p)` doesn't work either, it executes immediately
and evaluates to `void`, not a pointer to anything). `handleEffectSwitch()`
ends up just calling `modeEffectSwitchAction()` when it's set, with zero
awareness of which mode (or which parameter) is behind it — exactly the kind
of decoupling `effectControls[][]` and these helpers are about.

[⬆ Back to top](#table-of-contents)

## Automatic mapping example: Mode 3 ("archetypeVSTglobalcontrols")

Mode 3 is the "automatic mapping" counterpart to the Solar System walkthrough
above: a complete worked example of a self-contained mode that drives *named*
`effectControls[][]` parameters via `direct_controls.h`'s helpers, instead of
either plugging into the shared pickup machinery (like mode 1) or inventing
its own private state (like mode 4's planets). The code lives in
`midi_a14h.ino`: `drawScreenArchetypeVSTglobalcontrols()` and `case 3:` in
`updateModeIO()`.

### What it does

Exposes a handful of the `OTHER` section's controls directly: input/output
gain on knobs 1 and 5, an on/off toggle (`effectControls[OTHER][4]`, the
"on_off" switch) on footswitch A *and*
the effect switch, and a group of four on/off switches
(indices 5/6/7/2 — "Pre FX"/"Post FX"/"Cab"/"doubler",
`vstSwitchOptions[]`) that knob 3 picks from and the physical 3-way switch
then drives.

### The screen: `drawScreenArchetypeVSTglobalcontrols()`

Draws the header and one row per control, each showing its label and current
value (or "?" while `.known` is still false, see "`effectControls[][]`
structure" above):

```cpp
const Parameter& inGain = effectControls[OTHER][0] /* OTHER_INPUT_GAIN */;
snprintf(buf, sizeof(buf), "%d", inGain.value);
drawVstRow(11, inGain.label, inGain.known ? buf : "?");
```

`drawVstRow()` is a small local helper that mirrors the look of the shared
parameter rows without reaching into `display.cpp`'s private drawing
functions, keeping the mode self-contained. Like mode 4's screen, it's only
ever redrawn on `displayRefreshNeeded` (set by the helpers below whenever
something actually changes).

### The per-tick behavior: `case 3:` in `updateModeIO()`

**1. RGB LEDs and ring**: footswitches painted plain white (this mode doesn't
use `effectLedColor`, it isn't browsing effects), `LED_MODE`/`LED_EFFECT`
turned off. The ring splits its 24 LEDs into 4 arcs, one per
`vstSwitchOptions[]` entry, all sharing a single warm-white "identity" color
(this is one selector, not four different effects, so unlike mode 2's ring it
doesn't recolor per option) with the currently selected arc left at full
brightness and the rest dimmed via `color.nscale8(5)` — the same "dim instead
of black" trick "`effectLedColor`" above describes.

**2. Footswitch A → the OTHER section's on/off switch, fully self-contained**: claims the switch
from the generic dispatcher with `modeOwnsFootswitchA = true` (see
`switches.h`; reset to `false` every tick by `updateModeIO()` before
dispatch, so ownership auto-releases the instant another mode becomes
current — without it, the generic momentary-pulse handler in
`updateSwitchesMidi()` would fire *alongside* this mode's own toggle and
double up the MIDI), then drives the parameter directly in one call:

```cpp
modeOwnsFootswitchA = true;
static ButtonEdge swA;
driveSwitchFromControl(PIN_SW_A, swA, effectControls[OTHER][4] /* OTHER_ON_OFF */);
```

**3. Effect switch → the same on/off toggle**: claims the short press
via `modeEffectSwitchAction` (see "Claiming the effect switch:
`modeEffectSwitchAction`" above for the full mechanism) and points it at a
captureless lambda that just calls the same toggle footswitch A uses, so
either control flips the same parameter:

```cpp
modeEffectSwitchAction = []() { driveToggleFromControl(effectControls[OTHER][4] /* OTHER_ON_OFF */); };
```

**4. Knobs 1 and 5 → input/output gain**, fully self-contained, direct
mapping (like mode 4's planets, no soft takeover, see `KNOB_MODE` above): one
call each replaces the whole read/map/compare/write/send/ring/redraw block
the Solar System walkthrough shows you how to write by hand,
`driveKnobFromControl()` does it all and leaves `p.value`/`p.known` correct
for the screen and any feedback that comes back:

```cpp
driveKnobFromControl(1, effectControls[OTHER][0] /* OTHER_INPUT_GAIN */);
driveKnobFromControl(5, effectControls[OTHER][1] /* OTHER_OUTPUT_GAIN */);
ringMidiValues[1] = 0;
ringMidiValues[2] = 0; // knob 3 is a selector, not a parameter: ring stays off
ringMidiValues[3] = 0;
```

**5. Knob 3 → which on/off option the 3-way switch drives**: read raw (same
`KNOB3_IS_ENDLESS`-aware pattern mode 2 uses to pick an effect), constrained
to a 0-3 index into `vstSwitchOptions[]`. `selChanged` records whether the
selection actually moved this tick, you'll see it again in the next step:

```cpp
int sel = constrain(map(readKnob(3), 0, 4095, 3, 0), 0, 3);
bool selChanged = (sel != lastSel);
if (selChanged) {
    lastSel              = sel;
    vstSelectedOption    = sel;
    displayRefreshNeeded = true;
}
```

**6. The 3-way switch → whichever option is currently selected**:
`drive3waySwitchFromControl()` is the 3-way-switch counterpart to
`driveSwitchFromControl()`/`driveKnobFromControl()`, give it the parameter
and it sends on physical movement, same ABSOLUTE-vs-TOGGLE convention as any
other on/off control. Its `resync` argument is what makes remapping the same
physical switch onto a *different* parameter mid-session work cleanly: pass
`true` the moment the mapping changes (mode just entered, or `selChanged`)
and it re-displays the newly-selected parameter's stored value without
sending anything, instead of firing off MIDI for a switch the user never
actually touched:

```cpp
drive3waySwitchFromControl(effectControls[OTHER][vstSwitchOptions[vstSelectedOption]],
                           modeJustEntered || selChanged);
```

`modeJustEntered` is a generic "did the active mode just change" signal
`updateModeIO()` computes once per tick (a `static int lastActiveMode`
compared against `mode`) and passes down: a `case` body only runs while its
mode is active, so it can't otherwise tell "I was just switched to" from "I'm
still active", unlike `updateThreeWaySwitch()` which runs every tick
regardless of mode and can watch `currentMode` change directly.

[⬆ Back to top](#table-of-contents)
