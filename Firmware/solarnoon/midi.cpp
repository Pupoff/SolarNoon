#include "midi.h"
#include "debug.h"
#include "leds_mono.h"         // setIS31Led(), ringBehaviors[], ringMidiValues[], ringIdx()
#include "Archetype-Henson.h"  // Section enum → SECTION_COUNT
#include <Control_Surface.h>
#include <tusb.h>              // tud_mounted()

// NimBLE headers for peer-info tracking (ESP32 with NimBLE stack only)
#if defined(CONFIG_BT_NIMBLE_ENABLED)
#if __has_include(<host/ble_gap.h>)
#include <host/ble_gap.h>
#else
#include <nimble/nimble/host/include/host/ble_gap.h>
#endif
#endif
#include "three_way_switch.h" // threeWayPosition
#include "knobs.h"            // readKnob()
#include "switches.h"     
//#include "leds_rgb.h"         // can't be called here, control_surface already here

// ── Globals ───────────────────────────────────────────────────────────────────

MidiControl  ringMidiMap[5];
LedMidiEntry ledMidiEntries[MAX_LED_ENTRIES];
int          ledMidiEntryCount = 0;
bool         displayRefreshNeeded = false;

// Boot-time BLE/USB selection via placement new — only one interface is ever
// constructed, so Control_Surface only registers the active one.
static bool bleMode = false;
alignas(USBMIDI_Interface)       static uint8_t usbBuf[sizeof(USBMIDI_Interface)];
alignas(BluetoothMIDI_Interface) static uint8_t bleBuf[sizeof(BluetoothMIDI_Interface)];
static MIDI_Interface* activeMidi = nullptr;

// ── MIDI mounted / feedback ───────────────────────────────────────────────────
// feedbackOk: true if a CC was received on MIDI_CHANNEL_RECEIVE within
// FEEDBACK_TIMEOUT_MS after the last sendMidiCC() call.
static bool          feedbackOk      = false;
static bool          feedbackPending = false;
static unsigned long feedbackSentAt  = 0;
#define FEEDBACK_TIMEOUT_MS 3000

// Echo suppression: feedback the DAW echoes back on MIDI_CHANNEL_RECEIVE always
// lags one round-trip behind our sends. If presses come faster than that
// round-trip, a stale echo of an OLDER send arrives after a NEWER send and
// would overwrite the just-toggled local value with outdated data — forcing
// extra presses to "win the race". Any feedback for a CC we ourselves sent
// recently is therefore treated as a pure round-trip echo (used only for
// feedbackOk) and never applied to parameters[][]/rings.
static uint8_t       lastSentCC = 0xFF;
static unsigned long lastSentAt = 0;
#define FEEDBACK_ECHO_MS 400

// In SWITCH_MIDI_MODE_TOGGLE, what comes back on MIDI_CHANNEL_RECEIVE for the
// CC we just pulsed is NOT an echo of our sent value (127/0 are just a trigger
// pulse) — it's the plugin reporting its actual resulting on/off state, which
// is exactly what we need to keep parameters[][].value (and the LED) correct.
// So echo suppression must be skipped for that feedback; this flag marks it.
static bool lastSendWasTrigger = false;

bool isMidiMounted() {
    if (bleMode) {
        if (!activeMidi) return false;
        return static_cast<BluetoothMIDI_Interface*>(activeMidi)->isConnected();
    }
    return tud_mounted();
}
bool getFeedbackOk() { return feedbackOk; }
bool isBleMode()     { return bleMode; }

void disableBle() {
    // Silence all outgoing MIDI; BLE hardware keeps running until reboot.
    activeMidi = nullptr;
}

int getBleBondCount() {
    return -1; // bond count API differs between Bluedroid and NimBLE backends
}

// ── BLE peer info tracking (NimBLE only) ─────────────────────────────────────
// A secondary GAP event listener — NimBLE supports multiple, no conflict.

#if defined(CONFIG_BT_NIMBLE_ENABLED)
static struct ble_gap_event_listener s_bleListener;
static char s_peerAddr[18]  = "---";
static bool s_peerBonded    = false;
static bool s_peerEncrypted = false;

static void updatePeerFromDesc(const struct ble_gap_conn_desc *d) {
    const uint8_t *a = d->peer_id_addr.val;
    snprintf(s_peerAddr, sizeof(s_peerAddr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             a[5], a[4], a[3], a[2], a[1], a[0]);
    s_peerBonded    = d->sec_state.bonded;
    s_peerEncrypted = d->sec_state.encrypted;
}

static int bleGapCb(struct ble_gap_event *event, void *) {
    struct ble_gap_conn_desc desc;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0 &&
                    ble_gap_conn_find(event->connect.conn_handle, &desc) == 0)
                updatePeerFromDesc(&desc);
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            strncpy(s_peerAddr, "---", sizeof(s_peerAddr));
            s_peerBonded = s_peerEncrypted = false;
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            // Fired when bonding completes after initial connect
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0)
                updatePeerFromDesc(&desc);
            break;
        default: break;
    }
    return 0;
}
#endif // CONFIG_BT_NIMBLE_ENABLED

BlePeerInfo getBlePeerInfo() {
    BlePeerInfo info;
    info.bonded    = false;
    info.encrypted = false;
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    strncpy(info.addr, s_peerAddr, sizeof(info.addr));
    info.bonded    = s_peerBonded;
    info.encrypted = s_peerEncrypted;
#else
    strncpy(info.addr, "---", sizeof(info.addr));
#endif
    return info;
}

// ── MIDI lookup table ─────────────────────────────────────────────────────────
// Built once at init. Maps MIDI CC number → parameters[section][param] index.

struct MidiLookupEntry { uint8_t section, param; bool valid; };
static MidiLookupEntry midiLookup[128];

void buildMidiLookup() {
    for (int n = 0; n < 128; n++) midiLookup[n].valid = false;
    for (int i = 0; i < SECTION_COUNT; i++) {
        for (int j = 0; j < MAX_PARAMS; j++) {
            int note = parameters[i][j].midiNote;
            // skip 0 (often uninitialized) and already-claimed notes
            if (note > 0 && note < 128
                    && parameters[i][j].label != nullptr
                    && !midiLookup[note].valid) {
                midiLookup[note] = {(uint8_t)i, (uint8_t)j, true};
            }
        }
    }
}

static bool findParamByCC(uint8_t cc, size_t &si, size_t &sj) {
    if (!midiLookup[cc].valid) return false;
    si = midiLookup[cc].section;
    sj = midiLookup[cc].param;
    return true;
}

// ── LED entry helpers ─────────────────────────────────────────────────────────

static void applyNonRGBState(const LedMidiEntry& e) {
    bool on = (e.value > 63);
    if (e.device.type == LED_DEVICE_IS31)
        setIS31Led(e.device.id, on ? 255 : 5);
    else if (e.device.type == LED_DEVICE_GPIO)
        digitalWrite(e.device.id, on ? HIGH : LOW);
    // RGB handled by updateLedMidiFeedback() in leds_rgb.cpp
}

static int findLedEntryByCC(uint8_t channel, uint8_t cc) {
    for (int i = 0; i < ledMidiEntryCount; i++)
        if (ledMidiEntries[i].valid
                && ledMidiEntries[i].control.channel == channel
                && ledMidiEntries[i].control.number  == cc) return i;
    return -1;
}

static void storeLedEntry(LedDevice device, MidiControl control, RGBColor onColor) {
    if (ledMidiEntryCount >= MAX_LED_ENTRIES) {
        DBG_PRINTLN("mapLEDtoMIDI: MAX_LED_ENTRIES reached");
        return;
    }
    LedMidiEntry& e = ledMidiEntries[ledMidiEntryCount++];
    e = {device, control, onColor, 0, true};
    applyNonRGBState(e); // initial off/dim state
}

// ── Incoming MIDI callback ────────────────────────────────────────────────────
// Called by Control_Surface.loop(). Only handles CC on MIDI_CHANNEL_RECEIVE.

static bool channelMessageCallback(ChannelMessage cm) {
    if ((cm.header & 0xF0) != 0xB0) return false; // CC messages only, any channel

    uint8_t ch  = (cm.header & 0x0F) + 1; // received channel, 1-indexed
    uint8_t cc  = cm.data1;
    uint8_t val = cm.data2;
    DBG_PRINTF("[RX] CC  ch=%d  cc=%3d  val=%3d\n", ch, cc, val);

    // ── Feedback detection ────────────────────────────────────────────────────
    if (ch == MIDI_CHANNEL_RECEIVE && feedbackPending) {
        feedbackOk      = true;
        feedbackPending = false;
    }

    // ── Update parameter + rings (MIDI_CHANNEL_RECEIVE only) ─────────────────
    bool isEchoOfOwnSend = !lastSendWasTrigger
                        && (cc == lastSentCC)
                        && (millis() - lastSentAt < FEEDBACK_ECHO_MS);
    if (ch == MIDI_CHANNEL_RECEIVE && !isEchoOfOwnSend) {
        size_t si, sj;
        if (findParamByCC(cc, si, sj)) {
            switch (parameters[si][sj].type) {
                case SWITCH:
                    parameters[si][sj].value = (val > 63) ? 1 : 0;
                    break;
                case THREE_WAY_SWITCH:
                    parameters[si][sj].value = (val < 43) ? 0 : (val < 86) ? 1 : 2;
                    break;
                default: // KNOB / SLIDER
                    parameters[si][sj].value = val * 100 / 127;
                    break;
            }
            parameters[si][sj].known = true;
            displayRefreshNeeded = true;
        }

        for (int k = 0; k < 5; k++) {
            if (ringMidiMap[k].channel == MIDI_CHANNEL_RECEIVE && ringMidiMap[k].number == cc)
                ringMidiValues[k] = val * 255 / 127;
        }
    }

    // ── Update LED entry (any channel — matched by entry's stored channel) ────
    int idx = findLedEntryByCC(ch, cc);
    if (idx >= 0) {
        ledMidiEntries[idx].value = val;
        applyNonRGBState(ledMidiEntries[idx]);
    }

    return false;
}

// ── Public API ────────────────────────────────────────────────────────────────

static bool sysExCallback      (SysExMessage)      { return false; }
static bool sysCommonCallback  (SysCommonMessage)  { return false; }
static bool realTimeCallback   (RealTimeMessage)   { return false; }

void initMidi() {
    pinMode(PIN_BLE_SW, INPUT_PULLUP);
    bleMode = digitalRead(PIN_BLE_SW) == HIGH;
    if (bleMode)
        activeMidi = new(bleBuf) BluetoothMIDI_Interface();
    else
        activeMidi = new(usbBuf) USBMIDI_Interface();

    Control_Surface.setMIDIInputCallbacks(
        channelMessageCallback,
        sysExCallback,
        sysCommonCallback,
        realTimeCallback
    );
    Control_Surface.begin();
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    if (bleMode)
        ble_gap_event_listener_register(&s_bleListener, bleGapCb, nullptr);
#endif
    // buildMidiLookup() called from setup() AFTER initParameters()
}

void updateMidi() {
    updateSwitchesMidi();
    updateKnobsMidi();
    updateSwitchMidi();
    updateMode3Switch();
    Control_Surface.loop();
    // Feedback timeout: if no response within FEEDBACK_TIMEOUT_MS, mark as no feedback
    if (feedbackPending && (millis() - feedbackSentAt) > FEEDBACK_TIMEOUT_MS) {
        feedbackOk      = false;
        feedbackPending = false;
    }
}

void mapLEDtoMIDI(LedDevice device, MidiControl control) {
    mapLEDtoMIDI(device, control, {255, 255, 255});
}

void mapLEDtoMIDI(LedDevice device, MidiControl control, RGBColor onColor) {
    switch (device.type) {
        case LED_DEVICE_RING: {
            int i = ringIdx(device.id);
            if (i < 0) { DBG_PRINTLN("mapLEDtoMIDI: ring out of range (1-5)"); return; }
            ringMidiMap[i]    = control;
            ringMidiValues[i] = 0;
            ringBehaviors[i]  = SYNC_WITH_MIDI_CUSTOM;
            break;
        }
        case LED_DEVICE_IS31:
        case LED_DEVICE_GPIO:
        case LED_DEVICE_RGB:
            storeLedEntry(device, control, onColor);
            break;
    }
}

void sendMidiCC(uint8_t ccNumber, uint8_t value) {
    if (!activeMidi) return;
    DBG_PRINTF("[TX] CC  ch=%d  cc=%3d  val=%3d\n", MIDI_CHANNEL_SEND, ccNumber, value);
    activeMidi->sendControlChange({ccNumber, Channel(MIDI_CHANNEL_SEND)}, value);
    feedbackPending = true;
    feedbackSentAt  = millis();
    lastSentCC         = ccNumber;
    lastSentAt         = feedbackSentAt;
    lastSendWasTrigger = false;
}

void sendMidiSwitch(uint8_t ccNumber, uint8_t newValue) {
#if SWITCH_MIDI_MODE == SWITCH_MIDI_MODE_TOGGLE
    // Momentary press+release pulse — the plugin toggles its own on/off state
    // on the rising edge (127), mirroring our local `value ^= 1`. Sending the
    // release (0) too matches what a real momentary footswitch sends and is
    // what most "toggle"-mode MIDI Learn assignments expect to see.
    (void)newValue;
    sendMidiCC(ccNumber, 127);
    sendMidiCC(ccNumber, 0);
    // The feedback that follows reports the plugin's actual resulting on/off
    // state (not an echo of our 127/0 pulse) — let it through so it can
    // correct parameters[][].value / the effect LED if our local prediction
    // (the XOR toggle) ever drifts from the plugin's real state.
    lastSendWasTrigger = true;
#else
    sendMidiCC(ccNumber, newValue ? 127 : 0);
#endif
}

