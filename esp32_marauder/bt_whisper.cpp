// ============================================================================
// bt_whisper.cpp — WhisperPair (assessment-only Fast Pair probe)
//
// Background: Google Fast Pair providers advertise service data under UUID
// 0xFE2C and expose a GATT service (also 0xFE2C) whose "Key-Based Pairing"
// characteristic accepts a request frame from an unauthenticated central:
//   frame = [0x00][6B seeker address][6B random ephemeral key][optional flags]
// A healthy provider either ignores the frame or rejects it; per Google's
// spec the response (if any) is a notification containing the echoed random.
//
// This module SENDS the documented request frame and RECORDS the response,
// then reports a verdict. It does not derive keys, does not complete
// pairing, and does not claim any specific CVE status — treat results as an
// assessment input, exactly like HaleHound's description of the module.
// Verify any finding against Google's public Fast Pair documentation and
// responsible-disclosure channels before drawing conclusions.
// ============================================================================
#include "bt_whisper.h"
#include "bt_compat.h"

static bool s_running = false;
static bool s_probing = false;
static int  s_probeSlot = -1;
static uint32_t s_found = 0;

// --------------------------------------------------------------- scan sink -
static void whisper_sink(const bt_dev_t* dev, const uint8_t* adv, size_t len,
                         int rssi) {
    if (!s_running) return;
    size_t p = 0;
    while (p + 1 < len) {
        uint8_t flen = adv[p];
        if (flen == 0 || p + 1 + flen > len) break;
        if (adv[p + 1] == 0x16 && flen >= 6) {
            uint16_t u = adv[p + 2] | (adv[p + 3] << 8);
            if (u == 0xFE2C) {
                bt_dev_t* mut = const_cast<bt_dev_t*>(dev);
                mut->kind = 0xFE;              // tag: fast pair provider
                s_found++;
                uint32_t now = millis();
                if (now - dev->firstSeen < 60) {  // first hit: announce once
                    Serial.printf("[wp] Fast Pair provider %s %ddBm \"%s\" model=%02X%02X%02X\n",
                                  bt_mac_str(dev->mac).c_str(), rssi, dev->name,
                                  adv[p + 5], adv[p + 6], adv[p + 7]);
                }
                return;
            }
        }
        p += 1 + flen;
    }
}

void bt_whisper_register() { bt_scan_add_sink(whisper_sink); }

// ------------------------------------------------------------------- probe --
static bool wp_probe(uint8_t slot) {
    bt_dev_t* d = bt_dev_get(slot);
    if (!d || d->kind != 0xFE) {
        Serial.println("[wp] that index is not a tagged Fast Pair provider (run `wp scan`)");
        return false;
    }
    if (!bt_loot_ready()) bt_loot_open("whisperpair");

    NimBLEAddress addr(d->mac, d->addrType);
    NimBLEClient* cl = NimBLEDevice::createClient();
    if (!cl) return false;
    cl->setConnectTimeout(BT_CONN_TIMEOUT_MS / 1000);
    Serial.printf("[wp] probing %s ...\n", bt_mac_str(d->mac).c_str());
    if (!cl->connect(addr)) {
        Serial.println("[wp] connect failed (provider may only accept known seekers)");
        NimBLEDevice::deleteClient(cl);
        return false;
    }

    NimBLEUUID svcUuid((uint16_t)0xFE2C);
    NimBLERemoteService* svc = cl->getService(svcUuid);
    String verdict = "no FP service exposed";
    String detail  = "";
    if (svc) {
        std::vector<NimBLERemoteCharacteristic*> chrs = svc->getCharacteristics(true);
        Serial.printf("[wp] FP service found, %u characteristics\n", (unsigned)chrs.size());
        // seeker address: use a random static-looking address
        uint8_t frame[13];
        frame[0] = 0x00;                       // message type: key-based pairing
        for (int i = 0; i < 6; i++) frame[1 + i] = random(256);
        for (int i = 0; i < 6; i++) frame[7 + i] = random(256);  // ephemeral key
        bool anyWriteOk = false;
        for (NimBLERemoteCharacteristic* ch : chrs) {
            uint8_t props = bt_chr_props(ch);
            if (!(props & (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NR))) continue;
            bool ok = ch->writeValue(frame, sizeof(frame), true);
            anyWriteOk |= ok;
            Serial.printf("[wp] KBP write h=%03u rc=%s\n", ch->getHandle(), ok ? "ok" : "fail");
            if (ok) {
                // short read-back to catch providers that answer via value
                vTaskDelay(pdMS_TO_TICKS(400));
                if (props & BLE_GATT_CHR_F_READ) {
                    NimBLEAttValue v = ch->readValue();
                    if (v.size()) detail = bt_hex_str((const uint8_t*)v.data(), v.size());
                }
            }
        }
        verdict = anyWriteOk
            ? (detail.length() ? "RESPONDED to unauthenticated KBP write — REVIEW"
                               : "accepted unauthenticated KBP write (no response captured)")
            : "rejected/ignored KBP write";
    }

    bt_loot_writeln("{\"ts\":" + String(millis()) +
                    ",\"module\":\"whisperpair\",\"type\":\"probe\"" +
                    ",\"mac\":\"" + bt_mac_str(d->mac) + "\"" +
                    ",\"name\":\"" + bt_json_escape(String(d->name)) + "\"" +
                    ",\"verdict\":\"" + bt_json_escape(verdict) + "\"" +
                    ",\"detail\":\"" + detail + "\"}");
    bt_loot_close();
    Serial.printf("[wp] verdict: %s\n", verdict.c_str());

    cl->disconnect();
    NimBLEDevice::deleteClient(cl);
    return true;
}

// ----------------------------------------------------------------- command --
bool bt_whisper_command(const String& c) {
    if (!c.startsWith("wp")) return false;
    String a = (c.length() > 2) ? c.substring(2) : "";
    a.trim();

    if (a.startsWith("scan")) {
        String r = a.substring(4); r.trim();
        uint32_t secs = r.length() ? r.toInt() : 20;
        s_running = true;
        bt_dev_clear();
        s_found = 0;
        bt_scan_start(secs * 1000);
        Serial.printf("[wp] scanning %lus for Fast Pair (0xFE2C) providers\n",
                      (unsigned long)secs);
        return true;
    }
    if (a.startsWith("probe")) {
        if (!bt_is_armed()) {
            Serial.println("[wp] refused — run `bt arm` first (assessment touches the device)");
            return true;
        }
        int idx = a.substring(5).toInt();
        s_probing = true;
        wp_probe(idx);
        s_probing = false;
        return true;
    }
    if (a == "stop") { bt_whisper_stop(); return true; }
    Serial.println("usage: wp scan [secs] | wp probe <idx> | wp stop");
    return true;
}

void bt_whisper_stop() {
    s_running = false; s_probing = false;
    if (bt_scan_running()) bt_scan_stop();
}

void bt_whisper_loop() {
    if (s_running && !bt_scan_running() && !s_probing) {
        Serial.printf("[wp] scan done, %u Fast Pair providers seen\n", (unsigned)s_found);
        s_running = false;
    }
}

bool bt_whisper_status(BtUiState& out) {
    if (!s_running && !s_probing) return false;
    out.title = "WhisperPair"; out.running = true;
    out.phase = s_probing ? "probing" : "scanning";
    out.module = BT_MOD_WHISPER;
    out.lineCount = 0;
    out.lines[out.lineCount++] = bt_fmt("FP providers: %u", (unsigned)s_found);
    out.lines[out.lineCount++] = bt_fmt("devices: %d", bt_dev_count());
    return true;
}
