// ============================================================================
// bt_karr.cpp — KARR (detect + assess, verdict-only)
//
// HaleHound's KARR targets SouthWest Dealer Services BLE car alarms. This
// implementation is signature-driven and TRANSPARENT: the signature table
// below ships with generic aftermarket-alarm indicators and grows at runtime
// via `karr sig <namePrefix> <uuid16>`. We do NOT fabricate SWDS-specific
// identifiers — capture them in the field with `tracker scan` / nRF Connect
// and add them here.
//
// Verdict logic (passive only):
//   DETECTED   — signature match on name prefix and/or advertised 16-bit svc
//   ASSESS     — optional arm-gated connect that counts GATT characteristics
//                (generation hint: many custom write-only chars = older gen)
//   No commands are ever written to the vehicle.
// ============================================================================
#include "bt_karr.h"
#include "bt_compat.h"

struct KarrSig { char prefix[12]; uint16_t svc16; };
static KarrSig s_sigs[16] = {
    // generic aftermarket/alarm BLE module indicators — extend in the field
    { "BT",   0xFFF0 },
    { "JDY",  0xFFE0 },
    { "HM",   0xFFE0 },
    { "AT-09",0xFFE0 },
};
static uint8_t s_sigCount = 4;

static bool s_running = false;
static int  s_hits = 0;

static bool sig_match(const bt_dev_t* dev, const uint8_t* adv, size_t len) {
    if (dev->name[0]) {
        for (uint8_t i = 0; i < s_sigCount; i++)
            if (s_sigs[i].prefix[0] &&
                strncmp(dev->name, s_sigs[i].prefix, strlen(s_sigs[i].prefix)) == 0)
                return true;
    }
    size_t p = 0;
    while (p + 1 < len) {
        uint8_t flen = adv[p];
        if (flen == 0 || p + 1 + flen > len) break;
        if (adv[p + 1] == 0x02 && flen >= 3) {          // incomplete svc list
            for (uint8_t o = 0; o + 2 <= flen - 1; o += 2) {
                uint16_t u = adv[p + 2 + o] | (adv[p + 3 + o] << 8);
                for (uint8_t i = 0; i < s_sigCount; i++)
                    if (s_sigs[i].svc16 && s_sigs[i].svc16 == u) return true;
            }
        }
        p += 1 + flen;
    }
    return false;
}

static void karr_sink(const bt_dev_t* dev, const uint8_t* adv, size_t len, int rssi) {
    if (!s_running) return;
    if (!sig_match(dev, adv, len)) return;
    bt_dev_t* mut = const_cast<bt_dev_t*>(dev);
    mut->kind = 0xAA;                       // tag: karr hit
    s_hits++;
    uint32_t now = millis();
    if (now - dev->firstSeen < 60) {
        Serial.printf("[karr] HIT %s %ddBm \"%s\"\n",
                      bt_mac_str(dev->mac).c_str(), rssi, dev->name);
        Serial.println("[karr] verdict: DETECTED (passive). Run `karr assess <idx>` "
                       "for a GATT-layout assessment (requires arm; writes nothing).");
    }
}

void bt_karr_register() { bt_scan_add_sink(karr_sink); }

static void karr_assess(uint8_t slot) {
    bt_dev_t* d = bt_dev_get(slot);
    if (!d || d->kind != 0xAA) { Serial.println("[karr] not a tagged hit"); return; }
    NimBLEAddress addr(d->mac, d->addrType);
    NimBLEClient* cl = NimBLEDevice::createClient();
    if (!cl) return;
    cl->setConnectTimeout(BT_CONN_TIMEOUT_MS / 1000);
    Serial.printf("[karr] assessing %s (read-only GATT discovery)\n",
                  bt_mac_str(d->mac).c_str());
    if (!cl->connect(addr)) { Serial.println("[karr] connect failed"); NimBLEDevice::deleteClient(cl); return; }

    std::vector<NimBLERemoteService*> svcs = cl->getServices(true);
    uint16_t customChars = 0, writeOnly = 0;
    for (NimBLERemoteService* svc : svcs) {
        NimBLEUUID su = svc->getUUID();
        uint16_t id = su.bitSize() == 16 ? *(uint16_t*)su.getValue() : 0;
        if (id >= 0xFF00) {
            std::vector<NimBLERemoteCharacteristic*> chrs = svc->getCharacteristics(true);
            customChars += chrs.size();
            for (NimBLERemoteCharacteristic* ch : chrs) {
                uint8_t p = bt_chr_props(ch);
                if ((p & BLE_GATT_CHR_F_WRITE) && !(p & BLE_GATT_CHR_F_READ)) writeOnly++;
            }
        }
    }
    const char* gen = (customChars >= 6 && writeOnly >= 2)
        ? "older-generation module (many custom write-only chars) — LIKELY INTERESTING"
        : "layout looks like generic BLE serial — LOWER INTEREST";
    Serial.printf("[karr] verdict: custom chars=%u write-only=%u -> %s\n",
                  customChars, writeOnly, gen);
    bt_loot_open("karr");
    bt_loot_writeln("{\"ts\":" + String(millis()) +
                    ",\"module\":\"karr\",\"type\":\"assess\"" +
                    ",\"mac\":\"" + bt_mac_str(d->mac) + "\"" +
                    ",\"custom_chars\":" + String(customChars) +
                    ",\"write_only\":" + String(writeOnly) + "}");
    bt_loot_close();
    cl->disconnect();
    NimBLEDevice::deleteClient(cl);
}

bool bt_karr_command(const String& c) {
    if (!c.startsWith("karr")) return false;
    String a = c.substring(4); a.trim();

    if (a.startsWith("scan")) {
        String r = a.substring(4); r.trim();
        uint32_t secs = r.length() ? r.toInt() : 30;
        s_running = true; s_hits = 0;
        bt_dev_clear();
        bt_scan_start(secs * 1000);
        Serial.printf("[karr] passive scan %lus\n", (unsigned long)secs);
        return true;
    }
    if (a.startsWith("assess")) {
        if (!bt_is_armed()) { Serial.println("[karr] refused — run `bt arm` first"); return true; }
        karr_assess(a.substring(6).toInt());
        return true;
    }
    if (a.startsWith("sig")) {
        // karr sig <namePrefix> <uuid16|->
        String r = a.substring(3); r.trim();
        int sp = r.indexOf(' ');
        if (sp < 0) { Serial.println("usage: karr sig <namePrefix> <uuid16|->"); return true; }
        String pfx = r.substring(0, sp);
        String u16 = r.substring(sp + 1); u16.trim();
        if (s_sigCount >= 16) { Serial.println("[karr] sig table full"); return true; }
        KarrSig& s = s_sigs[s_sigCount++];
        strncpy(s.prefix, pfx.c_str(), 11); s.prefix[11] = 0;
        s.svc16 = u16 == "-" ? 0 : (uint16_t)strtol(u16.c_str(), nullptr, 16);
        Serial.printf("[karr] sig added: \"%s\" svc=0x%04X\n", s.prefix, s.svc16);
        return true;
    }
    if (a == "list") {
        Serial.println("[karr] signatures:");
        for (uint8_t i = 0; i < s_sigCount; i++)
            Serial.printf("  \"%s\" svc=0x%04X\n", s_sigs[i].prefix, s_sigs[i].svc16);
        return true;
    }
    if (a == "stop") { bt_karr_stop(); return true; }
    Serial.println("usage: karr scan [secs] | karr assess <idx> | karr sig <pfx> <uuid16|-> | karr list | karr stop");
    return true;
}

void bt_karr_stop() { s_running = false; if (bt_scan_running()) bt_scan_stop(); }

void bt_karr_loop() {
    if (s_running && !bt_scan_running()) { Serial.println("[karr] scan done"); s_running = false; }
}

bool bt_karr_status(BtUiState& out) {
    if (!s_running) return false;
    out.title = "KARR"; out.running = true; out.phase = "watching";
    out.module = BT_MOD_KARR;
    out.lineCount = 0;
    out.lines[out.lineCount++] = bt_fmt("hits: %d", s_hits);
    out.lines[out.lineCount++] = bt_fmt("devices: %d", bt_dev_count());
    return true;
}
