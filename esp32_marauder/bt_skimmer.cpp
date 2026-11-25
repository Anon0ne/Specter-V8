// ============================================================================
// bt_skimmer.cpp — Skimmer Hunt (BETA, heuristic)
//
// Scoring signals (each additive, threshold 60):
//   +25  serial-ish module name (HC-05/06/08, HM-10, AT-09, JDY-31, BT05...)
//   +20  Nordic UART Service (6E400001...) advertised or connected
//   +15  vendor FFF0/FFE0 service, no manufacturer data
//   +15  high advertisement burst rate (>= 12 frames/10s — spoofed intensity)
//   +10  static public address with no name at all
//   +10  device reappears with rotated random MAC but same name (table hits)
// This is a HEURISTIC tool for authorized fraud-prevention field work. It
// can false-positive on legitimate maker modules; treat >=60 as "investigate",
// not as proof. The active pass (arm-gated) connects and checks for a
// write-only serial characteristic to firm the score up.
// ============================================================================
#include "bt_skimmer.h"
#include "bt_compat.h"
#include <strings.h>

static bool     s_running = false;
static uint32_t s_alerts  = 0;
static uint32_t s_frames  = 0;

struct SScore { uint8_t mac[6]; uint16_t burst; uint32_t windowMs; uint8_t score; };
static SScore s_scores[BT_MAX_DEVS];
static uint8_t s_scoreCount = 0;

static const char* SK_NAMES[] = {
    "HC-05", "HC-06", "HC-08", "HM-10", "HM-13", "AT-09", "JDY-31", "JDY-08",
    "BT05", "BT06", "CC41", "MLT-BT05", "SPP-CA", "PAX", "INGENICO", "POS-"
};
static const uint8_t SK_NAME_COUNT = sizeof(SK_NAMES) / sizeof(SK_NAMES[0]);

static bool name_match(const char* name) {
    if (!name[0]) return false;
    for (uint8_t i = 0; i < SK_NAME_COUNT; i++)
        if (strncasecmp(name, SK_NAMES[i], strlen(SK_NAMES[i])) == 0) return true;
    return false;
}

static bool has_svc16(const uint8_t* adv, size_t len, uint16_t want) {
    size_t p = 0;
    while (p + 1 < len) {
        uint8_t flen = adv[p];
        if (flen == 0 || p + 1 + flen > len) break;
        if ((adv[p + 1] == 0x02 || adv[p + 1] == 0x03) && flen >= 3) {
            for (uint8_t o = 0; o + 2 <= flen - 1; o += 2) {
                uint16_t u = adv[p + 2 + o] | (adv[p + 3 + o] << 8);
                if (u == want) return true;
            }
        }
        if (adv[p + 1] == 0x16 && flen >= 3) {
            uint16_t u = adv[p + 2] | (adv[p + 3] << 8);
            if (u == want) return true;
        }
        p += 1 + flen;
    }
    return false;
}

static bool has_mfg(const uint8_t* adv, size_t len) {
    size_t p = 0;
    while (p + 1 < len) {
        uint8_t flen = adv[p];
        if (flen == 0 || p + 1 + flen > len) break;
        if (adv[p + 1] == 0xFF) return true;
        p += 1 + flen;
    }
    return false;
}

static SScore* score_for(const bt_dev_t* dev) {
    for (uint8_t i = 0; i < s_scoreCount; i++)
        if (memcmp(s_scores[i].mac, dev->mac, 6) == 0) return &s_scores[i];
    if (s_scoreCount >= BT_MAX_DEVS) return nullptr;
    SScore& s = s_scores[s_scoreCount++];
    memset(&s, 0, sizeof(s));
    memcpy(s.mac, dev->mac, 6);
    return &s;
}

static void skimmer_sink(const bt_dev_t* dev, const uint8_t* adv, size_t len, int rssi) {
    if (!s_running) return;
    s_frames++;
    SScore* s = score_for(dev);
    if (!s) return;

    // burst rate in 10s windows
    uint32_t now = millis();
    if (now - s->windowMs > 10000) { s->windowMs = now; s->burst = 0; }
    s->burst++;

    uint8_t score = 0;
    if (name_match(dev->name)) score += 25;
    if (has_svc16(adv, len, 0x6E40)) score += 20;          // NUS 128-bit prefix
    if (has_svc16(adv, len, 0xFFF0) || has_svc16(adv, len, 0xFFE0)) score += 15;
    if (!has_mfg(adv, len)) score += 15;
    if (s->burst >= 12) score += 15;
    if (!dev->name[0] && dev->addrType == 0) score += 10;
    s->score = score;

    if (score >= 60 && s->burst <= 12) {                   // alert once per window
        s_alerts++;
        Serial.printf("[skimmer] SCORE %u — %s %ddBm \"%s\" INVESTIGATE\n",
                      score, bt_mac_str(dev->mac).c_str(), rssi, dev->name);
        bt_loot_open("skimmer");
        bt_loot_writeln("{\"ts\":" + String(now) +
                        ",\"module\":\"skimmer\",\"type\":\"alert\"" +
                        ",\"mac\":\"" + bt_mac_str(dev->mac) + "\"" +
                        ",\"rssi\":" + String(rssi) +
                        ",\"name\":\"" + bt_json_escape(String(dev->name)) + "\"" +
                        ",\"score\":" + String(score) + "}");
        bt_loot_close();
    }
}

void bt_skimmer_register() { bt_scan_add_sink(skimmer_sink); }

static void skimmer_verify(uint8_t slot) {
    bt_dev_t* d = bt_dev_get(slot);
    if (!d) { Serial.println("[skimmer] bad index"); return; }
    NimBLEAddress addr(d->mac, d->addrType);
    NimBLEClient* cl = NimBLEDevice::createClient();
    if (!cl) return;
    cl->setConnectTimeout(BT_CONN_TIMEOUT_MS / 1000);
    Serial.printf("[skimmer] active confirmation on %s ...\n", bt_mac_str(d->mac).c_str());
    if (!cl->connect(addr)) { Serial.println("[skimmer] connect failed (score stays passive)"); NimBLEDevice::deleteClient(cl); return; }
    bool writeOnlySerial = false;
    std::vector<NimBLERemoteService*> svcs = cl->getServices(true);
    for (NimBLERemoteService* svc : svcs) {
        NimBLEUUID su = svc->getUUID();
        uint16_t id = su.bitSize() == 16 ? *(uint16_t*)su.getValue() : 0;
        if (id == 0xFFE0 || id == 0xFFF0 || id == 0xFFF3 ||
            (su.bitSize() == 128 && su.toString().rfind("6e400001", 0) == 0)) {
            std::vector<NimBLERemoteCharacteristic*> chrs = svc->getCharacteristics(true);
            for (NimBLERemoteCharacteristic* ch : chrs) {
                uint8_t p = bt_chr_props(ch);
                if ((p & BLE_GATT_CHR_F_WRITE) && !(p & BLE_GATT_CHR_F_READ))
                    writeOnlySerial = true;
            }
        }
    }
    Serial.printf("[skimmer] confirmation: %s\n", writeOnlySerial
        ? "write-only serial char present — RAISE SCORE (classic skimmer profile)"
        : "no write-only serial char — score unchanged");
    bt_loot_open("skimmer");
    bt_loot_writeln("{\"ts\":" + String(millis()) +
                    ",\"module\":\"skimmer\",\"type\":\"verify\"" +
                    ",\"mac\":\"" + bt_mac_str(d->mac) + "\"" +
                    ",\"write_only_serial\":" + (writeOnlySerial ? "true" : "false") + "}");
    bt_loot_close();
    cl->disconnect();
    NimBLEDevice::deleteClient(cl);
}

bool bt_skimmer_command(const String& c) {
    if (!c.startsWith("skimmer")) return false;
    String a = c.substring(7); a.trim();

    if (a.startsWith("scan")) {
        s_running = true; s_alerts = 0; s_frames = 0; s_scoreCount = 0;
        bt_dev_clear();
        bt_scan_start(0);
        Serial.println("[skimmer] passive hunting (Ctrl: skimmer stop)");
        return true;
    }
    if (a.startsWith("verify")) {
        if (!bt_is_armed()) { Serial.println("[skimmer] refused — run `bt arm` first"); return true; }
        skimmer_verify(a.substring(6).toInt());
        return true;
    }
    if (a == "list") {
        Serial.printf("[skimmer] scores:\n");
        for (uint8_t i = 0; i < s_scoreCount; i++)
            Serial.printf("  %s score=%u burst=%u\n", bt_mac_str(s_scores[i].mac).c_str(),
                          s_scores[i].score, s_scores[i].burst);
        return true;
    }
    if (a == "stop") { bt_skimmer_stop(); return true; }
    Serial.println("usage: skimmer scan | skimmer list | skimmer verify <idx> | skimmer stop");
    return true;
}

void bt_skimmer_stop() { s_running = false; if (bt_scan_running()) bt_scan_stop(); }

void bt_skimmer_loop() { /* pure callback driven */ }

bool bt_skimmer_status(BtUiState& out) {
    if (!s_running) return false;
    out.title = "Skimmer Hunt"; out.running = true; out.phase = "BETA hunting";
    out.module = BT_MOD_SKIMMER;
    out.lineCount = 0;
    out.lines[out.lineCount++] = bt_fmt("alerts: %lu", (unsigned long)s_alerts);
    out.lines[out.lineCount++] = bt_fmt("frames: %lu", (unsigned long)s_frames);
    out.lines[out.lineCount++] = bt_fmt("tracked: %u", s_scoreCount);
    return true;
}
