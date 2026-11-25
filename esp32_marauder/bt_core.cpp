// ============================================================================
// bt_core.cpp — shared infrastructure implementation
// ============================================================================
#include "configs.h" // Project HAS_SD/HAS_BT features must be visible in this TU.
#include "bt_core.h"
#include "bt_badble.h"

#include <NimBLEDevice.h>
#include "bt_compat.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <stdarg.h>
#include "hale_team.h"   // HALE: Red/Blue team mode gate
#include "SpecterSessions.h"  // HALE: session storage + busy-state integration
#ifdef HAS_SD
#include <SD.h>
#include "SDInterface.h"
extern SDInterface sd_obj;
#endif

#if BT_ENABLE_TRACKER
  #include "bt_tracker.h"
#endif
#if BT_ENABLE_PHANTOM
  #include "bt_phantom.h"
#endif
#if BT_ENABLE_BEACON
  #include "bt_beacon.h"
#endif
#if BT_ENABLE_SPOOFER
  #include "bt_spoofer.h"
#endif
#if BT_ENABLE_PREDATOR
  #include "bt_predator.h"
#endif
#if BT_ENABLE_SKELETON
  #include "bt_skeleton.h"
#endif
#if BT_ENABLE_WHISPER
  #include "bt_whisper.h"
#endif
#if BT_ENABLE_KARR
  #include "bt_karr.h"
#endif
#if BT_ENABLE_SKIMMER
  #include "bt_skimmer.h"
#endif

// ============================================================ module state ==
static bool        s_sinksRegistered = false;
static bool        s_armConfirmWin   = false;
static uint32_t    s_armConfirmT0    = 0;
static bool        s_armed           = false;
static Preferences s_prefs;

bool (*s_wifi_pre)(void)  = nullptr;
void (*s_wifi_post)(void) = nullptr;

// ================================================================== radio ===
static bool s_hostReady = false;

bool bt_radio_ready() { return s_hostReady; }

// HALE: report BLE busy to Specter Sessions (blocks session start/end during jobs)
bool bt_busy() { return bt_scan_running() || bt_tx_active() || bt_badble_busy(); }

bool bt_radio_begin() {
    if (s_hostReady) return true;
    if (s_wifi_pre) s_wifi_pre();          // host tears down WiFi first
    NimBLEDevice::init("");
    s_hostReady = true;
    Serial.println("[bt] NimBLE host ready");
    return true;
}

static void bt_forget_scan_host();

void bt_radio_end() {
    if (!s_hostReady) return;
    const uint32_t before = ESP.getFreeHeap();
    bt_stop_all();
    bt_tx_stop();
    bt_scan_stop();
    // Retained server objects cache started/handle state after the host GATT
    // database is cleared. Drop them together; scanner-only teardown can retain
    // its objects. Persistent peer bonds are not erased by deinit(clearAll).
    const bool clearObjects = NimBLEDevice::getServer() != nullptr;
    if (!NimBLEDevice::deinit(false)) {
        Serial.println("[bt] NimBLE host release failed");
        return;
    }
    if (clearObjects) {
        // Stop succeeded. Clear native objects only after the host is down;
        // deinit(true) can delete them even if its stop operation fails.
        NimBLEDevice::deinit(true);
        bt_badble_host_released();
        bt_forget_scan_host();
    }
    s_hostReady = false;
    Serial.printf("[bt] heap released: %lu -> %lu\n",
                  (unsigned long)before, (unsigned long)ESP.getFreeHeap());
    if (s_wifi_post) s_wifi_post();        // host restores WiFi
    Serial.println("[bt] NimBLE host released");
}

void bt_set_radio_hooks(bool (*pre)(void), void (*post)(void)) {
    s_wifi_pre  = pre;
    s_wifi_post = post;
}

// ========================================================= raw advertising ==
static bool     s_txActive = false;
static uint8_t  s_advBuf[BT_ADV_MAX_LEN];
static size_t   s_advLen = 0;
static uint16_t s_advInterval = 100;
static bool     s_advRotateAddr = true;
static uint32_t s_advNextMs = 0;
static uint8_t  s_randAddr[6];
static uint32_t s_advRepeats = 0;

static void gen_random_static_addr(uint8_t* a) {
    for (int i = 0; i < 6; i++) a[i] = random(256);
    a[5] = (a[5] & 0x3F) | 0xC0;           // two MSBs set => static random
}

bool bt_tx_raw(const uint8_t* adv, size_t len, uint16_t interval_ms,
               bool rotate_random_addr) {
    if (!adv || len == 0 || len > BT_ADV_MAX_LEN) return false;
    if (!bt_radio_begin()) return false;
    if (bt_scan_running()) bt_scan_stop(); // one radio, one job

    if (s_txActive) ble_gap_adv_stop();

    memcpy(s_advBuf, adv, len);
    s_advLen         = len;
    s_advInterval    = (interval_ms < 20) ? 20 : interval_ms;
    s_advRotateAddr  = rotate_random_addr;
    s_txActive       = true;
    s_advRepeats     = 0;
    s_advNextMs      = 0;                  // arm immediately on next tick
    return true;
}

bool bt_tx_rearm() {                       // used by modules to swap payload
    if (!s_txActive) return false;
    ble_gap_adv_stop();
    s_advNextMs = 0;
    return true;
}

void bt_tx_stop() {
    if (!s_txActive) return;
    ble_gap_adv_stop();
    s_txActive = false;
    s_advLen   = 0;
}

bool bt_tx_active() { return s_txActive; }

void bt_tx_tick() {
    if (!s_txActive) return;
    uint32_t now = millis();
    if (s_advNextMs != 0 && now < s_advNextMs) return;

    if (s_advRotateAddr) {
        gen_random_static_addr(s_randAddr);
        ble_hs_id_set_rnd(s_randAddr);
    }

    ble_gap_adv_params advp;
    memset(&advp, 0, sizeof(advp));
    advp.conn_mode             = BLE_GAP_CONN_MODE_NON;
    advp.disc_mode             = BLE_GAP_DISC_MODE_NON;
    const uint32_t units62     = s_advInterval / 0.625f;
    advp.itvl_min              = units62;
    advp.itvl_max              = units62;

    ble_gap_adv_stop();
    int rc = ble_gap_adv_set_data(s_advBuf, s_advLen);
    if (rc != 0) {
        Serial.printf("[bt] adv set_data rc=%d\n", rc);
        s_txActive = false;
        return;
    }
    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER,
                           &advp, NULL, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        Serial.printf("[bt] adv start rc=%d\n", rc);
        s_txActive = false;
        return;
    }
    s_advRepeats++;
    s_advNextMs = now + s_advInterval;
}

uint32_t bt_tx_repeats() { return s_advRepeats; }

// ================================================================= scanner ==
static NimBLEScan*  s_scan       = nullptr;
static bool         s_scanCbReg  = false;
static void bt_forget_scan_host() { s_scan = nullptr; s_scanCbReg = false; }
static uint32_t     s_scanStopMs = 0;    // 0 = until stopped
static bt_sink_fn   s_sinks[6];
static uint8_t      s_sinkCount  = 0;

static bt_dev_t s_devs[BT_MAX_DEVS];
static uint8_t  s_devCount = 0;

static int dev_find(const uint8_t* mac, uint8_t addrType) {
    for (int i = 0; i < s_devCount; i++)
        if (s_devs[i].addrType == addrType &&
            memcmp(s_devs[i].mac, mac, 6) == 0) return i;
    return -1;
}

static int dev_add(const uint8_t* mac, uint8_t addrType) {
    int slot;
    if (s_devCount >= BT_MAX_DEVS) {
        // Reuse one slot without dropping the rest of the visible device table.
        uint32_t oldest = UINT32_MAX; slot = 0;
        for (int i = 0; i < s_devCount; i++)
            if (s_devs[i].lastSeen < oldest) { oldest = s_devs[i].lastSeen; slot = i; }
    } else slot = s_devCount++;
    bt_dev_t* d = &s_devs[slot];
    memset(d, 0, sizeof(bt_dev_t));
    memcpy(d->mac, mac, 6);
    d->addrType = addrType;
    d->rssi = INT8_MIN; // A zero initial value rejects normal negative RSSI samples.
    d->firstSeen = d->lastSeen = millis();
    return slot;
}

void bt_scan_add_sink(bt_sink_fn fn) {
    if (fn && s_sinkCount < 6) s_sinks[s_sinkCount++] = fn;
}

// Parse one AD structure chain and store name into dev (if any).
static void extract_name(const uint8_t* adv, size_t len, char* out, size_t outMax) {
    out[0] = 0;
    size_t p = 0;
    while (p + 1 < len) {
        uint8_t flen = adv[p];
        if (flen == 0 || p + 1 + flen > len) break;
        uint8_t ftype = adv[p + 1];
        if (ftype == 0x09 || ftype == 0x08) {           // complete/short name
            size_t n = flen - 1;
            if (n > outMax - 1) n = outMax - 1;
            memcpy(out, &adv[p + 2], n);
            out[n] = 0;
            return;
        }
        p += 1 + flen;
    }
}

class BtScanCb : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        auto pl = dev->getPayload();          // std::string (1.x) / NimBLEAttValue (2.x)
        const uint8_t* adv = (const uint8_t*)pl.data();
        size_t len = pl.size();
        if (len == 0) return;
        if (len > BT_ADV_MAX_LEN) len = BT_ADV_MAX_LEN;

        NimBLEAddress a = dev->getAddress();
        uint8_t mac[6];
        memcpy(mac, a.getVal(), 6);                  // native byte order
        uint8_t at = (uint8_t)a.getType();

        int idx = dev_find(mac, at);
        if (idx < 0) idx = dev_add(mac, at);
        bt_dev_t* d = &s_devs[idx];
        d->lastSeen = millis();
        d->hits++;
        int rssi = dev->getRSSI();
        if (rssi > d->rssi) d->rssi = rssi;
        memcpy(d->payload, adv, len);
        d->plen = len;
        // core-known metadata: bit7 = connectable advertisement
        if (dev->isConnectable()) d->flags |= 0x80; else d->flags &= (uint8_t)~0x80;
        char nm[BT_NAME_MAX];
        extract_name(adv, len, nm, sizeof(nm));
        if (nm[0]) strncpy(d->name, nm, BT_NAME_MAX - 1);

        for (uint8_t i = 0; i < s_sinkCount; i++)
            if (s_sinks[i]) s_sinks[i](d, adv, len, rssi);
    }
};
static BtScanCb s_scanCb;

void bt_scan_start(uint32_t duration_ms) {
    if (!bt_radio_begin()) return;
    if (s_txActive) bt_tx_stop();
    if (!s_scan) s_scan = bt_get_scan();
    if (!s_scanCbReg) { s_scan->setScanCallbacks(&s_scanCb, false); s_scanCbReg = true; }
    s_scan->setActiveScan(BT_SCAN_ACTIVE);
    s_scan->setInterval(BT_SCAN_INTERVAL);
    s_scan->setWindow(BT_SCAN_WINDOW);
#if NIMBLE_MAJOR_VERSION >= 2
    const uint32_t apiDuration = duration_ms; // NimBLE 2.x takes milliseconds.
#else
    const uint32_t apiDuration = duration_ms / 1000 + (duration_ms % 1000 != 0);
#endif
    if (!s_scan->start(apiDuration, false)) {
        s_scanStopMs = 0;
        Serial.println("[bt] scan start failed");
        return;
    }
    s_scanStopMs = (duration_ms == 0) ? 0 : millis() + duration_ms;
}

void bt_scan_stop() {
    if (s_scan) s_scan->stop();
    s_scanStopMs = 0;
}

bool bt_scan_running() {
    return s_scan && s_scan->isScanning();
}

int  bt_dev_count()   { return s_devCount; }
void bt_dev_clear()   { s_devCount = 0; }

bt_dev_t* bt_dev_get(uint8_t idx) {
    if (idx >= s_devCount) return nullptr;
    return &s_devs[idx];
}

String bt_dev_line(uint8_t idx) {
    bt_dev_t* d = bt_dev_get(idx);
    if (!d) return "";
    String s = "[";
    s += String(idx) + "] " + bt_mac_str(d->mac) + " " + String(d->rssi) + "dBm";
    if (d->name[0]) { s += " \"" + String(d->name) + "\""; }
    return s;
}

// =================================================================== loot ===
static fs::FS* s_fs = nullptr;
static bool    s_sd = false;
static File    s_lootFile;
static String  s_lootModule;
static String  s_lootPath;
static uint32_t s_lootFlushMs = 0;
static String  s_lootBuf;
static uint16_t s_lootSeq = 0;

void bt_storage_init(fs::FS* sdFs) {
    s_fs = sdFs;
    s_sd = (sdFs != nullptr);
    if (!s_fs) {
        if (!LittleFS.begin(false)) {
            Serial.println("[bt] no SD, no LittleFS — loot disabled");
            return;
        }
        s_fs = &LittleFS;
    }
    Serial.printf("[bt] loot on %s\n", s_sd ? "SD" : "LittleFS");
}

static bool open_unique_loot(fs::FS& fs, const String& dir, const char* module) {
    for (uint32_t attempt = 0; attempt < 65535; ++attempt) {
        if (++s_lootSeq == 0) ++s_lootSeq;
        s_lootPath = dir + "/" + module + "_" + String(s_lootSeq) + ".jsonl";
        if (fs.exists(s_lootPath)) {
            if (attempt % 16 == 0) yield();
            continue;
        }
        s_lootFile = fs.open(s_lootPath, FILE_WRITE);
        if (!s_lootFile) { s_lootPath = ""; return false; }
        s_lootModule = module;
        return true;
    }
    s_lootPath = "";
    Serial.println("[bt] no unused loot filename available");
    return false;
}

bool bt_loot_open(const char* module) {
#ifdef HAS_SD
    // Active Specter session: group BLE loot under the session's output dir and index it.
    if (specter::sessionActive()) {
        String sdir = specter::sessionDirectory("bluetooth");
        if (sdir.length() == 0) {          // session active but no dir -> report, no silent fallback
            Serial.println("[bt] session active but no output dir; loot not opened");
            return false;
        }
        if (!open_unique_loot(SD, sdir, module)) return false;
        if (!specter::registerOutput(s_lootPath, "ble-report")) {
            s_lootFile.close();
            SD.remove(s_lootPath);
            s_lootPath = "";
            Serial.println("[bt] cannot index session loot; logging disabled");
            return false;
        }
        return true;
    }
#endif
    if (!s_fs) bt_storage_init(nullptr);
    if (!s_fs) return false;
    String dir = String(s_sd ? BT_LOOT_ROOT_SD : BT_LOOT_ROOT_FLASH) + "/" + module;
    s_fs->mkdir(String(s_sd ? BT_LOOT_ROOT_SD : BT_LOOT_ROOT_FLASH));
    s_fs->mkdir(dir);
    return open_unique_loot(*s_fs, dir, module);
}

void bt_loot_writeln(const String& jsonl) {
    if (!s_lootFile) return;
    s_lootBuf += jsonl;
    s_lootBuf += "\n";
    if (millis() - s_lootFlushMs > BT_LOOT_FLUSH_MS) {
        s_lootFile.print(s_lootBuf);
        s_lootBuf = "";
        s_lootFlushMs = millis();
    }
}

void bt_loot_close() {
    if (!s_lootFile) return;
    if (s_lootBuf.length()) s_lootFile.print(s_lootBuf);
    s_lootBuf = "";
    s_lootFile.close();
}

bool bt_loot_ready()   { return (bool)s_lootFile; }
String bt_loot_path()  { return s_lootPath; }

String bt_loot_summary() {
    if (!s_fs) return "loot storage not initialized";
    String base = String(s_sd ? BT_LOOT_ROOT_SD : BT_LOOT_ROOT_FLASH);
    String out  = "loot @ " + (s_sd ? String("SD:") : String("flash:")) + base;
    File dir = s_fs->open(base);
    if (!dir || !dir.isDirectory()) return out + " (empty)";
    while (File m = dir.openNextFile()) {
        out += "\n  " + String(m.name());
        m.close();
    }
    return out;
}

// =================================================================== misc ===
String bt_fmt(const char* fmt, ...) {        // printf-style into a String (UI lines)
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return String(buf);
}

String bt_mac_str(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    return String(buf);
}

String bt_hex_str(const uint8_t* d, size_t n, char sep) {
    String s;
    s.reserve(n * 3);
    char b[8];
    for (size_t i = 0; i < n; i++) {
        snprintf(b, sizeof(b), "%02X", d[i]);
        s += b;
        if (sep && i + 1 < n) s += sep;
    }
    return s;
}

String bt_json_escape(const String& in) {
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if ((uint8_t)c < 0x20) out += ' ';
        else out += c;
    }
    return out;
}

String bt_ascii_dump(const uint8_t* d, size_t n) {
    String s;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = d[i];
        s += (c >= 0x20 && c < 0x7F) ? (char)c : '.';
    }
    return s;
}

bool bt_parse_hex6(const String& s, uint8_t out[6]) {
    // accepts AA:BB:CC:DD:EE:FF or AABBCCDDEEFF (stored MSB-first display order)
    uint8_t tmp[6];
    int n = 0;
    for (size_t i = 0; i < s.length() && n < 6; i++) {
        char c = s[i];
        if (c == ':' || c == '-' || c == ' ') continue;
        int v = (c >= '0' && c <= '9') ? c - '0'
              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0) return false;
        if (n % 2 == 0) tmp[n / 2] = v << 4;
        else            tmp[n / 2] |= v;
        n++;
    }
    if (n != 12) return false;
    // display order AA:BB:CC:DD:EE:FF -> NimBLE native byte order reversed
    for (int i = 0; i < 6; i++) out[i] = tmp[5 - i];
    return true;
}

bool bt_parse_uuid(const String& hex, const char* base128, uint8_t out[16]) {
    String h = hex;
    h.trim();
    h.replace("0x", "");
    h.replace("-", "");
    if (h.length() == 4) {
        // 16-bit -> expand into full 128-bit base UUID
        if (!base128) return false;
        uint8_t b16[2];
        int v = strtol(h.c_str(), nullptr, 16);
        b16[0] = (v >> 8) & 0xFF; b16[1] = v & 0xFF;
        // base: 0000XXXX-0000-1000-8000-00805F9B34FB (little-endian out)
        static const uint8_t base[16] = {
            0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        memcpy(out, base, 16);
        out[12] = b16[1]; out[13] = b16[0];
        return true;
    }
    if (h.length() == 32) {
        for (int i = 0; i < 16; i++) {
            char b[3] = { h[30 - 2 * i], h[31 - 2 * i], 0 };
            out[i] = strtol(b, nullptr, 16);
        }
        return true;
    }
    return false;
}

// ============================================================ arm gate ======
bool bt_is_armed() {
#if HALE_TEAM_MODE
    if (!hale::offensiveAllowed()) return false;   // Blue Team blocks armed/TX ops
#endif
    return s_armed;
}

String bt_arm_begin() {
    s_armConfirmWin = true;
    s_armConfirmT0  = millis();
    return String(
        "\n=== AUTHORIZED USE REQUIRED ===\n"
        "The Bluetooth transmit/active modules you are about to unlock are\n"
        "offensive security tools. Use them ONLY on devices, networks and\n"
        "trackers you own or have explicit written authorization to test\n"
        "(laws such as the CFAA, EU GDPR/NIS2 and local penal codes apply).\n"
        "Confirm within 30 seconds:\n"
        "  bt arm yes\n");
}

bool bt_arm_confirm() {
    if (!s_armConfirmWin || millis() - s_armConfirmT0 > 30000) {
        s_armConfirmWin = false;
        return false;
    }
    s_armConfirmWin = false;
#if HALE_TEAM_MODE
    if (!hale::offensiveAllowed()) {
        Serial.println("[bt] Blue Team mode — cannot arm. Switch to Red Team in Settings.");
        return false;
    }
#endif
    s_armed = true;
    s_prefs.begin(BT_ARM_NVS_NS, false);
    s_prefs.putBool(BT_ARM_NVS_KEY, true);
    s_prefs.end();
    Serial.println("[bt] OFFENSIVE MODULES ARMED for this session.");
    return true;
}

void bt_disarm() {
    s_armed = false;
    s_prefs.begin(BT_ARM_NVS_NS, false);
    s_prefs.putBool(BT_ARM_NVS_KEY, false);
    s_prefs.end();
    Serial.println("[bt] disarmed.");
}

// ========================================================= sink registry ====
static void register_module_sinks() {
    if (s_sinksRegistered) return;
    s_sinksRegistered = true;
#ifdef HAS_SD
    bt_storage_init(sd_obj.supported ? &SD : nullptr);
#else
    bt_storage_init(nullptr);
#endif
    specter::setExternalBusyHook(bt_busy);   // HALE: Sessions sees BLE jobs as busy
#if BT_ENABLE_TRACKER
    bt_tracker_register();
#endif
#if BT_ENABLE_KARR
    bt_karr_register();
#endif
#if BT_ENABLE_SKIMMER
    bt_skimmer_register();
#endif
#if BT_ENABLE_PREDATOR
    bt_predator_register();
#endif
#if BT_ENABLE_WHISPER
    bt_whisper_register();
#endif
}

// ============================================================= dispatcher ===
static bool route_bt(const String& args) {          // "bt <sub> ..."
    String a = args; a.trim();
    if (a == "arm")              { Serial.println(bt_arm_begin()); return true; }
    if (a.startsWith("arm "))    {
        String yn = a.substring(4); yn.trim(); yn.toLowerCase();
        if (yn == "yes") { Serial.println(bt_arm_confirm() ? "ARMED." :
                            "confirmation window expired — run `bt arm` again"); }
        else Serial.println("usage: bt arm yes");
        return true;
    }
    if (a == "disarm")           { bt_disarm(); return true; }
    if (a == "status") {
        Serial.printf("pack: armed=%s host=%s scan=%s tx=%s loot=%s\n",
            bt_is_armed() ? "yes" : "no",
            bt_radio_ready() ? "up" : "down",
            bt_scan_running() ? "on" : "off",
            bt_tx_active() ? "on" : "off",
            bt_loot_ready() ? bt_loot_path().c_str() : "closed");
        return true;
    }
    if (a == "loot")             { Serial.println(bt_loot_summary()); return true; }
    if (a == "help" || a.length() == 0) { Serial.println(bt_help_text()); return true; }
    return false;
}

bool bt_command(const String& cmd) {
    register_module_sinks();
    String c = cmd; c.trim();

    if (c.length() == 0) return false;
    // tolerate one-shot arming: "bt arm yes"
    if (c.startsWith("bt ")) return route_bt(c.substring(3));

    if (c == "bb" || c.startsWith("bb ")) return bt_badble_command(c);
    if (c == "stopble") return bt_stop_all();
    if (bt_badble_busy()) {
        const char* modules[] = {"tracker", "phantom", "beacon", "spoofer",
            "predator", "sk", "wp", "karr", "skimmer"};
        for (const char* module : modules) {
            if (c == module || c.startsWith(String(module) + " ")) {
                Serial.println("[bb] Stop the keyboard before starting another BLE module");
                return true;
            }
        }
    }

#if BT_ENABLE_TRACKER
    if (c.startsWith("tracker")) return bt_tracker_command(c);
#endif
#if BT_ENABLE_PHANTOM
    if (c.startsWith("phantom")) return bt_phantom_command(c);
#endif
#if BT_ENABLE_BEACON
    if (c.startsWith("beacon"))  return bt_beacon_command(c);
#endif
#if BT_ENABLE_SPOOFER
    if (c.startsWith("spoofer")) return bt_spoofer_command(c);
#endif
#if BT_ENABLE_PREDATOR
    if (c.startsWith("predator"))return bt_predator_command(c);
#endif
#if BT_ENABLE_SKELETON
    if (c.startsWith("sk ") || c == "sk") return bt_skeleton_command(c);
#endif
#if BT_ENABLE_WHISPER
    if (c.startsWith("wp") )     return bt_whisper_command(c);
#endif
#if BT_ENABLE_KARR
    if (c.startsWith("karr"))    return bt_karr_command(c);
#endif
#if BT_ENABLE_SKIMMER
    if (c.startsWith("skimmer")) return bt_skimmer_command(c);
#endif
    return false;
}

// ==================================================================== loop ==
void bt_loop() {
    bt_badble_loop();
    bt_tx_tick();

    // scan timeout
    if (s_scanStopMs && static_cast<int32_t>(millis() - s_scanStopMs) >= 0) {
        bt_scan_stop();
        s_scanStopMs = 0;
    }

#if BT_ENABLE_TRACKER
    bt_tracker_loop();
#endif
#if BT_ENABLE_PHANTOM
    bt_phantom_loop();
#endif
#if BT_ENABLE_BEACON
    bt_beacon_loop();
#endif
#if BT_ENABLE_SPOOFER
    bt_spoofer_loop();
#endif
#if BT_ENABLE_PREDATOR
    bt_predator_loop();
#endif
#if BT_ENABLE_SKELETON
    bt_skeleton_loop();
#endif
#if BT_ENABLE_WHISPER
    bt_whisper_loop();
#endif
#if BT_ENABLE_KARR
    bt_karr_loop();
#endif
#if BT_ENABLE_SKIMMER
    bt_skimmer_loop();
#endif
}

// ================================================================ UI agg ====
bool bt_ui_get(BtUiState& out) {
    if (bt_badble_status(out)) return true;
#if BT_ENABLE_TRACKER
    if (bt_tracker_status(out))   return true;
#endif
#if BT_ENABLE_PHANTOM
    if (bt_phantom_status(out))   return true;
#endif
#if BT_ENABLE_BEACON
    if (bt_beacon_status(out))    return true;
#endif
#if BT_ENABLE_SPOOFER
    if (bt_spoofer_status(out))   return true;
#endif
#if BT_ENABLE_PREDATOR
    if (bt_predator_status(out))  return true;
#endif
#if BT_ENABLE_SKELETON
    if (bt_skeleton_status(out))  return true;
#endif
#if BT_ENABLE_WHISPER
    if (bt_whisper_status(out))   return true;
#endif
#if BT_ENABLE_KARR
    if (bt_karr_status(out))      return true;
#endif
#if BT_ENABLE_SKIMMER
    if (bt_skimmer_status(out))   return true;
#endif
    return false;
}

// ==================================================================== help ==
String bt_help_text() {
    return String(
        "bt arm | bt arm yes | bt disarm      authorization gate\n"
        "bt status | bt loot | bt help        pack status / loot index\n"
        "bb serve | bb list | bb run [file] | bb stop   keyboard scripts\n"
#if BT_ENABLE_TRACKER
        "tracker scan [secs] [log] | tracker list | tracker stop\n"
#endif
#if BT_ENABLE_PHANTOM
        "phantom start [count] | phantom stop | phantom lost on|off\n"
        "phantom replay <idx> [count]         rebroadcast captured FindMy adv\n"
#endif
#if BT_ENABLE_BEACON
        "beacon ibeacon <uuid> <major> <minor> [txp] | beacon url <url>\n"
        "beacon uid <nsHex> <instHex> | beacon tlm | beacon name <n>\n"
        "beacon interval <ms> | beacon stop\n"
#endif
#if BT_ENABLE_SPOOFER
        "spoofer list | spoofer apple <1-29|all> | spoofer android <hex6|all>\n"
        "spoofer swift [name] | spoofer interval <ms> | spoofer stop\n"
#endif
#if BT_ENABLE_PREDATOR
        "predator scan [secs] | predator recon <idx> | predator honeypot <idx>\n"
        "predator stop\n"
#endif
#if BT_ENABLE_SKELETON
        "sk connect <idx> | sk dump | sk fuzz all [n] | sk fuzz <svc> <chr> [n]\n"
        "sk led on|off|<RRGGBB> | sk stop\n"
#endif
#if BT_ENABLE_WHISPER
        "wp scan [secs] | wp probe <idx> | wp stop\n"
#endif
#if BT_ENABLE_KARR
        "karr scan [secs] | karr list | karr stop\n"
#endif
#if BT_ENABLE_SKIMMER
        "skimmer scan | skimmer list | skimmer verify <idx> | skimmer stop\n"
#endif
        "stopble                              stop all pack BLE activity\n");
}

// convenience: stop everything (single-word command, easy CLI hookup)
bool bt_stop_all() {
    bt_badble_stop();
#if BT_ENABLE_TRACKER
    bt_tracker_stop();
#endif
#if BT_ENABLE_PHANTOM
    bt_phantom_stop();
#endif
#if BT_ENABLE_BEACON
    bt_beacon_stop();
#endif
#if BT_ENABLE_SPOOFER
    bt_spoofer_stop();
#endif
#if BT_ENABLE_PREDATOR
    bt_predator_stop();
#endif
#if BT_ENABLE_SKELETON
    bt_skeleton_stop();
#endif
#if BT_ENABLE_WHISPER
    bt_whisper_stop();
#endif
#if BT_ENABLE_KARR
    bt_karr_stop();
#endif
#if BT_ENABLE_SKIMMER
    bt_skimmer_stop();
#endif
    return true;
}
