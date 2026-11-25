// ============================================================================
// bt_tracker.cpp — multi-platform BLE tracker scanner
//
// Detection model (all passive, from raw advertisement bytes):
//   Apple FindMy        : AD type 0xFF (mfg), company 0x004C, type 0x12,
//                         FindMy section len 0x19, 22-byte rolling pubkey.
//                         status byte bits: 0xC0 lost/unpublished state,
//                         0x30 accessory-category bits (see payload_tables.md)
//   Samsung SmartTag    : mfg 0x0075, len >= 19, first byte 0x01/0x02 (heuristic)
//   Tile                : mfg 0x0157, len >= 16 (status + 8B ID + 6B MAC)
//   Google FMDN         : service data UUID 0xFCD2 (Find Hub provider frames)
//   Chipolo ONE Spot    : FindMy-based -> surfaced via Apple FindMy branch
//
// Loot: JSONL to SD (bt_loot), throttled per device to avoid flooding.
// ============================================================================
#include "bt_tracker.h"

static bool        s_running   = false;
static bool        s_log       = false;
static BtTrackerStats s_stats;
static uint32_t    s_startedAt = 0;
static uint32_t    s_duration  = 0;      // 0 = until stop

// per-device throttling: log when first seen, then every BT_TRK_LOG_EVERY_MS
// or when rssi jumps. Indexed by device slot.
static uint32_t s_lastLog[BT_MAX_DEVS];

const char* BT_TRK_NAMES[] = {
    "unknown", "AirTag/FindMy item", "FindMy accessory", "Samsung SmartTag",
    "Tile", "Google FMDN", "Chipolo(FindMy)"
};

String bt_tracker_kind_name(uint8_t kind) {
    return String(BT_TRK_NAMES[kind <= BT_TRK_CHIPOLO_FINDMY ? kind : 0]);
}

BtTrackerStats* bt_tracker_stats() { return &s_stats; }

// -------------------------------------------------------------- detection --
static uint8_t classify(const uint8_t* adv, size_t len,
                        const uint8_t** appleBody, uint8_t* appleBodyLen) {
    size_t p = 0;
    while (p + 1 < len) {
        uint8_t flen = adv[p];
        if (flen == 0 || p + 1 + flen > len) break;
        const uint8_t* f = &adv[p + 1];
        uint8_t fl = flen;  // includes type byte

        if (f[0] == 0xFF && fl >= 5) {
            uint16_t comp = f[1] | (f[2] << 8);
            if (comp == 0x004C && f[3] == 0x12 && fl >= 31 - 2) {
                // Apple FindMy: [FF][4C 00][12][19][status][22B key][2B hint]
                if (f[4] == 0x19) {
                    if (appleBody) { *appleBody = &f[5]; *appleBodyLen = fl - 5; }
                    uint8_t status = f[5];
                    uint8_t cat = (status >> 4) & 0x03;
                    return cat == 0x00 ? BT_TRK_AIRTAG : BT_TRK_FINDMY_OTHER;
                }
            }
            if (comp == 0x0075) {
                // Samsung: heuristic — long mfg blob starting 0x01/0x02
                uint8_t dl = fl - 3;
                if (dl >= 19 && (f[3] == 0x01 || f[3] == 0x02))
                    return BT_TRK_SMARTTAG;
            }
            if (comp == 0x0157) {
                if (fl - 3 >= 16) return BT_TRK_TILE;
            }
        }
        if (f[0] == 0x16 && fl >= 3) {
            uint16_t u = f[1] | (f[2] << 8);
            if (u == 0xFCD2) return BT_TRK_FMDN;   // Find Hub provider frame
        }
        p += 1 + flen;
    }
    return BT_TRK_UNKNOWN;
}

// ------------------------------------------------------------------ sink ---
static void tracker_sink(const bt_dev_t* dev, const uint8_t* adv, size_t len,
                         int rssi) {
    if (!s_running) return;
    s_stats.frames++;

    uint8_t kind = classify(adv, len, nullptr, nullptr);
    if (kind == BT_TRK_UNKNOWN) return;

    // stamp classification onto the shared table (bit7 flags = core-managed)
    bt_dev_t* mut = const_cast<bt_dev_t*>(dev);
    mut->kind = kind;

    switch (kind) {
        case BT_TRK_AIRTAG:        if (s_stats.airtags < 0xFFFF) s_stats.airtags++; break;
        case BT_TRK_FINDMY_OTHER:
        case BT_TRK_CHIPOLO_FINDMY:if (s_stats.findmyOther < 0xFFFF) s_stats.findmyOther++; break;
        case BT_TRK_SMARTTAG:      if (s_stats.smarttags < 0xFFFF) s_stats.smarttags++; break;
        case BT_TRK_TILE:          if (s_stats.tiles < 0xFFFF) s_stats.tiles++; break;
        case BT_TRK_FMDN:          if (s_stats.fmdn < 0xFFFF) s_stats.fmdn++; break;
        default: break;
    }
    if (s_stats.total < 0xFFFF) s_stats.total++;

    // throttled JSONL loot — resolve device slot in the shared table
    int idx = -1;
    for (int i = 0; i < bt_dev_count(); i++)
        if (bt_dev_get(i) == dev) { idx = i; break; }
    if (idx < 0) return;

    uint32_t now = millis();
    bool logIt = now - s_lastLog[idx] > 15000;
    if (logIt) {
        s_lastLog[idx] = now;
        if (s_log && bt_loot_ready()) {
            // FindMy frame: [len][FF][4C 00][12][19][status][22B key][2B hint]
            const uint8_t statusByte = (kind == BT_TRK_AIRTAG ||
                                        kind == BT_TRK_FINDMY_OTHER) ? dev->payload[6] : 0;
            String j = "{\"ts\":" + String(now) +
                       ",\"module\":\"tracker\"" +
                       ",\"kind\":\"" + bt_json_escape(bt_tracker_kind_name(kind)) + "\"" +
                       ",\"mac\":\"" + bt_mac_str(dev->mac) + "\"" +
                       ",\"addr_type\":" + String(dev->addrType) +
                       ",\"rssi\":" + String(rssi) +
                       ",\"name\":\"" + bt_json_escape(String(dev->name)) + "\"" +
                       ",\"status\":\"0x" + String(statusByte, HEX) + "\"" +
                       ",\"lost_mode\":" + ((statusByte & 0xC0) ? "true" : "false") +
                       ",\"payload\":\"" + bt_hex_str(dev->payload, dev->plen) + "\"}";
            bt_loot_writeln(j);
        }
        Serial.printf("[tracker] %-18s %s %4ddBm %s\n",
                      bt_tracker_kind_name(kind).c_str(),
                      bt_mac_str(dev->mac).c_str(), rssi, dev->name);
    }
}

void bt_tracker_register() {
    bt_scan_add_sink(tracker_sink);
}

// ---------------------------------------------------------------- control --
bool bt_tracker_command(const String& c) {
    if (c == "tracker" || c.startsWith("tracker ")) {
        String a = (c.length() > 8) ? c.substring(8) : "";
        a.trim();
        if (a.startsWith("scan")) {
            uint32_t secs = 0;
            String rest = a.substring(4); rest.trim();
            bool wantLog = rest.endsWith(" log");
            if (wantLog) rest = rest.substring(0, rest.length() - 4);
            if (rest.length()) secs = rest.toInt();
            if (secs == 0) secs = 30;
            if (wantLog && !bt_loot_ready() && !bt_loot_open("tracker"))
                Serial.println("[tracker] logging unavailable; scanning without a log");
            s_log = wantLog && bt_loot_ready();
            bt_dev_clear();
            memset(s_lastLog, 0, sizeof(s_lastLog));
            memset(&s_stats, 0, sizeof(s_stats));
            s_running = true;
            s_startedAt = millis();
            s_duration = secs * 1000;
            bt_scan_start(s_duration);
            Serial.printf("[tracker] scanning %lus (log=%s)\n",
                          (unsigned long)secs, s_log ? "on" : "off");
            return true;
        }
        if (a == "list") {
            int n = bt_dev_count();
            Serial.printf("[tracker] %d devices\n", n);
            for (int i = 0; i < n; i++) {
                bt_dev_t* d = bt_dev_get(i);
                if (d->kind != BT_TRK_UNKNOWN)
                    Serial.printf("%s %s\n", bt_dev_line(i).c_str(),
                                  bt_tracker_kind_name(d->kind).c_str());
            }
            return true;
        }
        if (a == "stop") { bt_tracker_stop(); return true; }
        Serial.println("usage: tracker scan [secs] [log] | tracker list | tracker stop");
        return true;
    }
    return false;
}

void bt_tracker_stop() {
    s_running = false;
    if (bt_scan_running()) bt_scan_stop();
    bt_loot_close();
}

void bt_tracker_loop() {
    if (!s_running) return;
    if (s_duration && millis() - s_startedAt > s_duration + 500) {
        Serial.printf("[tracker] done. airTags=%u findmy=%u smartTags=%u tiles=%u fmdn=%u frames=%lu\n",
                      s_stats.airtags, s_stats.findmyOther, s_stats.smarttags,
                      s_stats.tiles, s_stats.fmdn, (unsigned long)s_stats.frames);
        bt_tracker_stop();
    }
}

bool bt_tracker_status(BtUiState& out) {
    if (!s_running) return false;
    out.title    = "Tracker Scan";
    out.running  = true;
    out.phase    = "scanning";
    out.module   = BT_MOD_TRACKER;
    out.lineCount = 0;
    out.lines[out.lineCount++] = bt_fmt("AirTag/FindMy: %u", s_stats.airtags + s_stats.findmyOther);
    out.lines[out.lineCount++] = bt_fmt("SmartTag: %u  Tile: %u", s_stats.smarttags, s_stats.tiles);
    out.lines[out.lineCount++] = bt_fmt("Google FMDN: %u", s_stats.fmdn);
    out.lines[out.lineCount++] = bt_fmt("frames: %lu", (unsigned long)s_stats.frames);
    // show up to 3 strongest hits
    int shown = 0;
    for (int i = 0; i < bt_dev_count() && shown < 3; i++) {
        bt_dev_t* d = bt_dev_get(i);
        if (d->kind == BT_TRK_UNKNOWN) continue;
        out.lines[out.lineCount++] = bt_dev_line(i);
        shown++;
    }
    return true;
}
