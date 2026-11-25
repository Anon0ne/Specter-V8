// ============================================================================
// bt_beacon.cpp — iBeacon / Eddystone broadcaster
//
// Frame builders per the published open specifications:
//  * iBeacon (Apple proximity beacon, mfg 0x004C type 0x02 len 0x15):
//      [02 01 06][1A FF 4C 00 02 15][16B proximity UUID][2B major][2B minor][1B TX]
//  * Eddystone (service data UUID 0xFEAA):
//      UID frame  [00] 10B namespace + 6B instance + 2B reserved
//      URL frame  [10] 1B TX power + encoded URL (prefix/suffix compression)
//      TLM frame  [20] version, battery, temp, adv count, uptime
// ============================================================================
#include "bt_beacon.h"

static bool     s_running = false;
static BtBeaconType s_type = BT_BCN_NONE;
static uint8_t  s_buf[31];
static size_t   s_len = 0;
static uint16_t s_interval = BT_BEACON_INTERVAL_MS;
static char     s_label[BT_NAME_MAX] = "beacon";

// ------------------------------------------------------------------ iBeacon
static bool build_ibeacon(const String& uuidHex, uint16_t major, uint16_t minor,
                          int8_t txp) {
    uint8_t uuid[16];
    if (!bt_parse_uuid(uuidHex, nullptr, uuid)) return false;
    memset(s_buf, 0, sizeof(s_buf));
    size_t p = 0;
    s_buf[p++] = 0x02; s_buf[p++] = 0x01; s_buf[p++] = 0x06;      // flags
    s_buf[p++] = 0x1A; s_buf[p++] = 0xFF;                         // mfg data, 26B
    s_buf[p++] = 0x4C; s_buf[p++] = 0x00;                         // Apple
    s_buf[p++] = 0x02; s_buf[p++] = 0x15;                         // iBeacon
    for (int i = 0; i < 16; i++) s_buf[p++] = uuid[15 - i];       // MSB-first on air
    s_buf[p++] = major >> 8; s_buf[p++] = major & 0xFF;
    s_buf[p++] = minor >> 8; s_buf[p++] = minor & 0xFF;
    s_buf[p++] = (uint8_t)txp;
    s_len = p;
    strncpy(s_label, "iBeacon", BT_NAME_MAX - 1);
    return true;
}

// -------------------------------------------------------- Eddystone URL enc
// Standard Eddystone-URL prefix/suffix tables (protocol constants)
static const char* kUrlPrefix[] = {
    "http://www.", "https://www.", "http://", "https://" };
static const char* kUrlSuffix[] = {
    ".com/", ".org/", ".edu/", ".net/", ".info/", ".biz/", ".gov/",
    ".com", ".org", ".edu", ".net", ".info", ".biz", ".gov" };

static bool encode_eddy_url(const String& urlIn, uint8_t* out, size_t* outLen,
                            int8_t txp) {
    String u = urlIn; u.toLowerCase();
    uint8_t scheme = 0xFF;
    for (uint8_t i = 0; i < 4; i++) {
        String p = kUrlPrefix[i];
        if (u.startsWith(p)) { scheme = i; u = u.substring(p.length()); break; }
    }
    if (scheme == 0xFF) { scheme = 2; }   // default http://

    uint8_t body[17];
    size_t bn = 0;
    const char* c = u.c_str();
    size_t i = 0;
    while (c[i] && bn < 17) {
        bool matched = false;
        for (uint8_t s = 0; s < 14; s++) {
            const char* suf = kUrlSuffix[s];
            size_t sl = strlen(suf);
            if (sl && strncmp(&c[i], suf, sl) == 0) {
                body[bn++] = s;
                i += sl;
                matched = true;
                break;
            }
        }
        if (!matched) body[bn++] = (uint8_t)c[i++];
    }
    if (bn == 0 || bn > 17) return false;

    size_t p = 0;
    s_buf[p++] = 0x02; s_buf[p++] = 0x01; s_buf[p++] = 0x06;
    // service data AD: len = 1(type)+2(uuid)+1(txp)+bn
    s_buf[p++] = (uint8_t)(1 + 2 + 1 + bn);
    s_buf[p++] = 0x16;
    s_buf[p++] = 0xAA; s_buf[p++] = 0xFE;                       // Eddystone
    s_buf[p++] = 0x10;                                          // URL frame
    s_buf[p++] = (uint8_t)txp;
    memcpy(&s_buf[p], body, bn); p += bn;
    s_buf[p++] = 0x03; s_buf[p++] = 0x03; s_buf[p++] = 0xAA; s_buf[p++] = 0xFE;
    s_len = p;
    snprintf(s_label, BT_NAME_MAX, "Eddystone URL");
    return true;
}

static bool build_eddy_uid(const String& nsHex, const String& instHex) {
    String ns = nsHex, in = instHex;
    ns.replace(" ", ""); in.replace(" ", "");
    if (ns.length() != 20 || in.length() != 12) return false;   // hex chars
    size_t p = 0;
    s_buf[p++] = 0x02; s_buf[p++] = 0x01; s_buf[p++] = 0x06;
    s_buf[p++] = 0x1B;                                          // 27B service data
    s_buf[p++] = 0x16;
    s_buf[p++] = 0xAA; s_buf[p++] = 0xFE;
    s_buf[p++] = 0x00;                                          // UID frame
    for (int i = 9; i >= 0; i--) {
        char b[3] = { ns[2 * i], ns[2 * i + 1], 0 };
        s_buf[p++] = (uint8_t)strtol(b, nullptr, 16);
    }
    for (int i = 5; i >= 0; i--) {
        char b[3] = { in[2 * i], in[2 * i + 1], 0 };
        s_buf[p++] = (uint8_t)strtol(b, nullptr, 16);
    }
    s_buf[p++] = 0x00; s_buf[p++] = 0x00;                       // reserved Tx/Cb
    s_len = p;
    snprintf(s_label, BT_NAME_MAX, "Eddystone UID");
    return true;
}

static bool build_eddy_tlm() {
    size_t p = 0;
    s_buf[p++] = 0x02; s_buf[p++] = 0x01; s_buf[p++] = 0x06;
    s_buf[p++] = 0x11;                                          // 17B service data
    s_buf[p++] = 0x16;
    s_buf[p++] = 0xAA; s_buf[p++] = 0xFE;
    s_buf[p++] = 0x20;                                          // TLM frame
    s_buf[p++] = 0x00;                                          // version
    s_buf[p++] = 0x00; s_buf[p++] = 0x00;                       // battery mV (0=unk)
    s_buf[p++] = 0x80; s_buf[p++] = 0x00;                       // temp 8.8 fixed
    uint32_t advCnt = bt_tx_repeats();
    uint32_t upSec  = millis() / 1000;
    memcpy(&s_buf[p], &advCnt, 2); p += 2;
    memcpy(&s_buf[p], &upSec, 2);  p += 2;
    s_len = p;
    snprintf(s_label, BT_NAME_MAX, "Eddystone TLM");
    return true;
}

// ------------------------------------------------------------------ control
bool bt_beacon_command(const String& c) {
    if (!c.startsWith("beacon")) return false;
    String a = c.substring(6); a.trim();

    if (a.startsWith("ibeacon")) {
        // ibeacon <uuid> <major> <minor> [txp]
        String r = a.substring(7); r.trim();
        int s1 = r.indexOf(' ');
        if (s1 < 0) { Serial.println("usage: beacon ibeacon <uuid> <major> <minor> [txp]"); return true; }
        String u = r.substring(0, s1); r = r.substring(s1 + 1); r.trim();
        int s2 = r.indexOf(' ');
        String majS = (s2 < 0) ? r : r.substring(0, s2);
        String minS = "", txpS = "";
        if (s2 >= 0) {
            r = r.substring(s2 + 1); r.trim();
            int s3 = r.indexOf(' ');
            minS = (s3 < 0) ? r : r.substring(0, s3);
            if (s3 >= 0) txpS = r.substring(s3 + 1); txpS.trim();
        }
        if (!bt_is_armed()) { Serial.println("[beacon] refused — run `bt arm` first"); return true; }
        if (build_ibeacon(u, majS.toInt(), minS.toInt(),
                          txpS.length() ? (int8_t)txpS.toInt() : (int8_t)-59)) {
            if (bt_tx_raw(s_buf, s_len, s_interval, false)) {
                s_running = true; s_type = BT_BCN_IBEACON;
                Serial.printf("[beacon] iBeacon %s major=%u minor=%u\n",
                              u.c_str(), majS.toInt(), minS.toInt());
            }
        } else Serial.println("[beacon] bad uuid (0000XXXX-0000-1000-8000-00805F9B34FB or 32 hex)");
        return true;
    }
    if (a.startsWith("url")) {
        String u = a.substring(3); u.trim();
        if (!bt_is_armed()) { Serial.println("[beacon] refused — run `bt arm` first"); return true; }
        if (u.length() && encode_eddy_url(u, s_buf, &s_len, (int8_t)-10)) {
            if (bt_tx_raw(s_buf, s_len, s_interval, false)) {
                s_running = true; s_type = BT_BCN_URL;
                Serial.printf("[beacon] Eddystone URL \"%s\"\n", u.c_str());
            }
        } else Serial.println("usage: beacon url <https://example.com> (max ~17 encoded bytes)");
        return true;
    }
    if (a.startsWith("uid")) {
        String r = a.substring(3); r.trim();
        int sp = r.indexOf(' ');
        if (sp < 0) { Serial.println("usage: beacon uid <nsHex10B> <instHex6B>"); return true; }
        if (!bt_is_armed()) { Serial.println("[beacon] refused — run `bt arm` first"); return true; }
        if (build_eddy_uid(r.substring(0, sp), r.substring(sp + 1))) {
            if (bt_tx_raw(s_buf, s_len, s_interval, false)) {
                s_running = true; s_type = BT_BCN_UID;
                Serial.println("[beacon] Eddystone UID on air");
            }
        } else Serial.println("[beacon] uid needs 20 + 12 hex chars");
        return true;
    }
    if (a == "tlm") {
        if (!bt_is_armed()) { Serial.println("[beacon] refused — run `bt arm` first"); return true; }
        if (build_eddy_tlm() && bt_tx_raw(s_buf, s_len, s_interval, false)) {
            s_running = true; s_type = BT_BCN_TLM;
            Serial.println("[beacon] Eddystone TLM on air");
        }
        return true;
    }
    if (a.startsWith("interval")) {
        int ms = a.substring(8).toInt();
        if (ms >= 20 && ms <= 10000) { s_interval = ms; Serial.printf("[beacon] interval %ums\n", ms); }
        else Serial.println("usage: beacon interval <20-10000>");
        return true;
    }
    if (a == "stop") { bt_beacon_stop(); return true; }
    Serial.println("usage: beacon ibeacon <uuid> <major> <minor> [txp] | beacon url <u> | beacon uid <ns> <inst> | beacon tlm | beacon interval <ms> | beacon stop");
    return true;
}

void bt_beacon_stop() {
    if (!s_running) return;
    s_running = false; s_type = BT_BCN_NONE;
    bt_tx_stop();
    Serial.println("[beacon] stopped");
}

void bt_beacon_loop() {
    if (!s_running) return;
    if (!bt_tx_active()) { s_running = false; return; }  // radio taken
    if (s_type == BT_BCN_TLM) {                          // refresh counters 1/s
        static uint32_t lastMs = 0;
        if (millis() - lastMs > 1000) {
            lastMs = millis();
            build_eddy_tlm();
            bt_tx_raw(s_buf, s_len, s_interval, false);
        }
    }
}

bool bt_beacon_status(BtUiState& out) {
    if (!s_running) return false;
    out.title    = "BLE Beacon";
    out.running  = true;
    out.phase    = "broadcasting";
    out.module   = BT_MOD_BEACON;
    out.lineCount = 0;
    out.lines[out.lineCount++] = String("type: ") + s_label;
    out.lines[out.lineCount++] = bt_fmt("interval: %ums", s_interval);
    out.lines[out.lineCount++] = bt_fmt("frames: %lu", (unsigned long)bt_tx_repeats());
    return true;
}
