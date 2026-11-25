// ============================================================================
// bt_predator.cpp — BLE Predator (scan / recon / honeypot)
//
// Threat classification uses well-known 16-bit GATT service IDs (Bluetooth
// SIG assigned numbers) plus a handful of vendor UUIDs commonly seen on
// consumer devices. Recon is a normal BLE central session. The honeypot
// mirrors the target's service/characteristic UUID layout into a NimBLE
// GATT server and records every write it receives (JSONL to SD).
// ============================================================================
#include "bt_predator.h"
#include "bt_compat.h"

static bool       s_scanActive = false;
static bool       s_reconActive = false;
static bool       s_honeypotActive = false;
static int        s_phaseIdx = -1;        // device table slot for recon/hp
static String     s_targetName;
static String     s_reconError;
static bool       s_reconDone = false;

static std::vector<BtGattCharInfo> s_recon;

// --------------------------------------------------- known-service labels --
struct SvcLabel { uint16_t id; const char* name; uint8_t interest; }; // 0-3
static const SvcLabel KNOWN_SVCS[] = {
    { 0x1800, "GAP", 0 },               { 0x1801, "GATT", 0 },
    { 0x180A, "Device Information", 1 },{ 0x180F, "Battery", 1 },
    { 0x1812, "HID (keyboard/mouse!)", 3 },
    { 0xFE2C, "Google Fast Pair", 2 },  { 0xFE59, "Nordic DFU / OTA", 2 },
    { 0xFFF0, "Vendor FFF0 (common)", 2 },{ 0xFFE0, "Vendor FFE0 (LED/serial)", 2 },
    { 0xFFD0, "Vendor FFD0 (LED strip)", 2 },{ 0xFF00, "Vendor FF00", 2 },
    { 0x180D, "Heart Rate", 1 },        { 0x1811, "Alert Notification", 1 },
    { 0x1815, "Automation IO", 2 },     { 0x181A, "Environment Sensing", 1 },
    { 0x1853, "Media Control", 1 },     { 0x1843, "AV Remote Control", 1 },
};
static const uint8_t KNOWN_SVC_COUNT = sizeof(KNOWN_SVCS) / sizeof(KNOWN_SVCS[0]);

static String label_for_uuid(const NimBLEUUID& u, uint8_t* interest) {
    *interest = 1;
    if (u.bitSize() == 16) {
        uint16_t v = *(uint16_t*)u.getValue();
        for (uint8_t i = 0; i < KNOWN_SVC_COUNT; i++)
            if (KNOWN_SVCS[i].id == v) { *interest = KNOWN_SVCS[i].interest;
                                        return String(KNOWN_SVCS[i].name); }
    }
    String hex = bt_hex_str(u.getValue(), u.bitSize()/8);
    return "custom " + hex;
}

// --------------------------------------------------------------- scan sink -
static void predator_sink(const bt_dev_t* dev, const uint8_t* adv, size_t len,
                          int rssi) {
    (void)adv; (void)len; (void)rssi;
    if (!s_scanActive) return;
    // connectable state is stamped by the scan core into dev->flags bit7;
    // announce each new connectable device once (first 50ms window)
    if ((dev->flags & 0x80) && (millis() - dev->firstSeen < 50)) {
        Serial.printf("[predator] connectable %s %s\n",
                      bt_mac_str(dev->mac).c_str(), dev->name);
    }
}

void bt_predator_register() { bt_scan_add_sink(predator_sink); }

// ------------------------------------------------------------------- recon --
static bool do_recon(uint8_t slot) {
    s_recon.clear();
    s_reconDone = false;
    s_reconError = "";
    s_targetName = "";
    bt_dev_t* d = bt_dev_get(slot);
    if (!d) { s_reconError = "Invalid device index"; Serial.println("[predator] " + s_reconError); return false; }
    if (!bt_radio_begin()) { s_reconError = "BLE host unavailable"; Serial.println("[predator] " + s_reconError); return false; }
    bt_scan_stop();
    if (!bt_loot_ready()) bt_loot_open("predator");

    Serial.printf("[predator] recon %s ...\n", bt_mac_str(d->mac).c_str());
    NimBLEAddress addr = bt_address_from_native(d->mac, d->addrType);
    NimBLEClient* cl = NimBLEDevice::createClient();
    if (!cl) { s_reconError = "Cannot allocate BLE client"; Serial.println("[predator] " + s_reconError); bt_loot_close(); return false; }
#if NIMBLE_MAJOR_VERSION >= 2
    cl->setConnectTimeout(BT_CONN_TIMEOUT_MS);
#else
    cl->setConnectTimeout(BT_CONN_TIMEOUT_MS / 1000);
#endif
    // Inspection works at the standard ATT MTU. Do not make link success depend
    // on automatic MTU exchange: NimBLE 2.3.8 can return EALREADY for that stage.
#if NIMBLE_MAJOR_VERSION >= 2
    const bool connected = cl->connect(addr, true, false, false);
#else
    const bool connected = cl->connect(addr);
#endif
    if (!connected) {
        s_reconError = String("Connect failed (NimBLE ") + String(cl->getLastError()) + ")";
        Serial.println("[predator] " + s_reconError);
        NimBLEDevice::deleteClient(cl);
        bt_loot_close();
        return false;
    }
    s_targetName = String(d->name);

    s_recon.clear();
    s_reconDone = false;

    std::vector<NimBLERemoteService*> svcs = cl->getServices(true);
    if (svcs.empty()) {
        s_reconError = String("No services (NimBLE ") + String(cl->getLastError()) + ")";
        Serial.println("[predator] " + s_reconError);
        cl->disconnect(); NimBLEDevice::deleteClient(cl); bt_loot_close();
        return false;
    }
    uint8_t si = 0, ci = 0;
    for (NimBLERemoteService* svc : svcs) {
        if (si++ >= BT_MAX_RECON_SVCS) break;
        NimBLEUUID su = svc->getUUID();
        std::vector<NimBLERemoteCharacteristic*> chrs = svc->getCharacteristics(true);
        for (NimBLERemoteCharacteristic* ch : chrs) {
            if (ci++ >= BT_MAX_RECON_CHARS) break;
            BtGattCharInfo info;
            info.handle = ch->getHandle();
            info.svcUuid = bt_hex_str(su.getValue(), su.bitSize()/8);
            info.chrUuid = bt_hex_str(ch->getUUID().getValue(), ch->getUUID().bitSize()/8);
            info.props = bt_chr_props(ch);
            if (info.props & BLE_GATT_CHR_F_READ) {
                NimBLEAttValue v = ch->readValue();
                info.valueHex = bt_hex_str((const uint8_t*)v.data(), v.size());
            }
            s_recon.push_back(info);
        }
    }

    if (s_recon.empty()) {
        s_reconError = "No characteristics returned";
        Serial.println("[predator] " + s_reconError);
        cl->disconnect(); NimBLEDevice::deleteClient(cl); bt_loot_close();
        return false;
    }

    // JSONL dump
    String svcJson = "[";
    for (size_t i = 0; i < s_recon.size(); i++) {
        const BtGattCharInfo& x = s_recon[i];
        svcJson += "{\"h\":" + String(x.handle) +
                   ",\"svc\":\"" + x.svcUuid + "\",\"chr\":\"" + x.chrUuid +
                   "\",\"p\":" + String(x.props) +
                   ",\"v\":\"" + x.valueHex + "\"}" +
                   (i + 1 < s_recon.size() ? "," : "");
    }
    svcJson += "]";
    bt_loot_writeln("{\"ts\":" + String(millis()) +
                    ",\"module\":\"predator\",\"type\":\"recon\"" +
                    ",\"mac\":\"" + bt_mac_str(d->mac) + "\"" +
                    ",\"name\":\"" + bt_json_escape(String(d->name)) + "\"" +
                    ",\"services\":" + svcJson + "}");
    bt_loot_close();

    uint8_t interest;
    Serial.printf("[predator] %u services, %u characteristics\n", si, ci);
    for (size_t i = 0; i < s_recon.size(); i++)
        Serial.printf("  h=%03u p=%02X %s\n", s_recon[i].handle, s_recon[i].props,
                      s_recon[i].chrUuid.c_str());
    Serial.printf("[predator] recon dump: %s\n", bt_loot_path().c_str());

    cl->disconnect();
    NimBLEDevice::deleteClient(cl);
    s_reconDone = true;
    return true;
}

// ---------------------------------------------------------------- honeypot --
static NimBLEServer*   s_srv = nullptr;
static NimBLEAdvertising* s_adv = nullptr;
static uint32_t s_hpWrites = 0;

class HpChrCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        NimBLEAttValue v = c->getValue();
        s_hpWrites++;
        String hex = bt_hex_str((const uint8_t*)v.data(), v.size());
        Serial.printf("[honeypot] WRITE h=%03u len=%u: %s |%s|\n",
                      c->getHandle(), (unsigned)v.size(), hex.c_str(),
                      bt_ascii_dump((const uint8_t*)v.data(), v.size()).c_str());
        if (bt_loot_ready()) {
            bt_loot_writeln("{\"ts\":" + String(millis()) +
                            ",\"module\":\"predator\",\"type\":\"honeypot_write\"" +
                            ",\"chr_handle\":" + String(c->getHandle()) +
                            ",\"len\":" + String((unsigned)v.size()) +
                            ",\"hex\":\"" + hex + "\"" +
                            ",\"ascii\":\"" +
                            bt_json_escape(bt_ascii_dump((const uint8_t*)v.data(), v.size())) +
                            "\"}");
        }
    }
};
static HpChrCb s_hpCb;

// host reset flag: after server teardown we note re-init need
static bool s_needReinit = false;
static void s_hostResetNeeded() { s_needReinit = true; }

static bool start_honeypot(uint8_t slot) {
    if (!s_reconDone || s_recon.empty()) {
        Serial.println("[predator] run `predator recon <idx>` first (honeypot mirrors the recon table)");
        return false;
    }
    bt_tx_stop();
    if (bt_scan_running()) bt_scan_stop();

    s_srv = NimBLEDevice::createServer();
    s_srv->setCallbacks(nullptr);

    // GAP name = target name so scanners see the familiar identity
    NimBLEDevice::setDeviceName(std::string(s_targetName.c_str()));

    uint8_t svcCount = 0, chrCount = 0;
    String lastSvc;
    NimBLEService* cur = nullptr;
    for (const BtGattCharInfo& x : s_recon) {
        if (svcCount >= BT_HONEYPOT_SVCS || chrCount >= BT_HONEYPOT_CHARS) break;
        if (x.svcUuid != lastSvc) {
            // build service UUID from the stored hex string
            NimBLEUUID su;
            String h = x.svcUuid;
            if (h.length() == 4) {
                uint16_t v = (uint16_t)strtol(h.c_str(), nullptr, 16);
                su = NimBLEUUID(v);
            } else if (h.length() == 32) {
                uint8_t be[16];
                for (int i = 0; i < 16; i++) {
                    char b[3] = { h[2 * i], h[2 * i + 1], 0 };
                    be[i] = (uint8_t)strtol(b, nullptr, 16);
                }
                su = NimBLEUUID(be, 16);
            } else continue;
            cur = s_srv->createService(su);
            if (cur) cur->start();
            lastSvc = x.svcUuid;
            svcCount++;
        }
        if (cur) {
            NimBLEUUID cu;
            String h = x.chrUuid;
            if (h.length() == 4) {
                cu = NimBLEUUID((uint16_t)strtol(h.c_str(), nullptr, 16));
            } else if (h.length() == 32) {
                uint8_t be[16];
                for (int i = 0; i < 16; i++) {
                    char b[3] = { h[2 * i], h[2 * i + 1], 0 };
                    be[i] = (uint8_t)strtol(b, nullptr, 16);
                }
                cu = NimBLEUUID(be, 16);
            } else continue;
            NimBLECharacteristic* ch =
                cur->createCharacteristic(cu, READ | WRITE | WRITE_NR, 128);
            if (ch) { ch->setCallbacks(&s_hpCb); chrCount++; }
        }
    }

    s_adv = NimBLEDevice::getAdvertising();
    s_adv->enableScanResponse(true);
    s_adv->start();
    s_hpWrites = 0;
    s_honeypotActive = true;
    if (!bt_loot_ready()) bt_loot_open("predator");
    Serial.printf("[predator] HONEYPOT live as \"%s\" (%u svc / %u chr) — writes are logged\n",
                  s_targetName.c_str(), svcCount, chrCount);
    return true;
}

static void stop_honeypot() {
    if (!s_honeypotActive) return;
    s_honeypotActive = false;
    if (s_adv) { s_adv->stop(); s_adv = nullptr; }
    // server object stays registered in the NimBLE host; we only drop our
    // references — a later start_honeypot() rebuilds a fresh GATT layout.
    s_srv = nullptr;
    bt_loot_close();
    Serial.printf("[predator] honeypot stopped (%lu writes captured)\n",
                  (unsigned long)s_hpWrites);
}

// ----------------------------------------------------------------- command --
bool bt_predator_command(const String& c) {
    if (!c.startsWith("predator")) return false;
    String a = c.substring(8); a.trim();

    if (a.startsWith("scan")) {
        String r = a.substring(4); r.trim();
        uint32_t secs = r.length() ? r.toInt() : 20;
        s_scanActive = true;
        bt_dev_clear();
        bt_scan_start(secs * 1000);
        Serial.printf("[predator] scanning %lus — connectable devices get marked (bit7)\n",
                      (unsigned long)secs);
        return true;
    }
    if (a.startsWith("recon")) {
        if (!bt_is_armed()) { Serial.println("[predator] refused — run `bt arm` first"); return true; }
        String r = a.substring(5); r.trim();
        int idx = r.toInt();
        s_reconActive = true;
        bool ok = do_recon(idx);
        s_reconActive = false;
        (void)ok;
        return true;
    }
    if (a.startsWith("honeypot")) {
        if (!bt_is_armed()) { Serial.println("[predator] refused — run `bt arm` first"); return true; }
        String r = a.substring(8); r.trim();
        s_phaseIdx = r.toInt();
        s_targetName = "";
        bt_dev_t* d = bt_dev_get(s_phaseIdx);
        if (d && d->name[0]) s_targetName = String(d->name);
        if (s_targetName.isEmpty()) s_targetName = "Predator";
        return start_honeypot(s_phaseIdx);
    }
    if (a == "stop") { bt_predator_stop(); return true; }
    Serial.println("usage: predator scan [secs] | predator recon <idx> | predator honeypot <idx> | predator stop");
    return true;
}

void bt_predator_stop() {
    s_scanActive = false;
    stop_honeypot();
    if (bt_scan_running()) bt_scan_stop();
}

void bt_predator_loop() {
    if (s_scanActive && !bt_scan_running() &&
        (bt_dev_count() > 0)) {
        // scan ended by duration; report connectable hits
        Serial.println("[predator] scan done, connectable devices:");
        for (int i = 0; i < bt_dev_count(); i++)
            if (bt_dev_get(i)->flags & 0x80)
                Serial.printf("  %s\n", bt_dev_line(i).c_str());
        s_scanActive = false;
    }
}

bool bt_predator_status(BtUiState& out) {
    if (s_honeypotActive) {
        out.title = "BLE Predator"; out.running = true; out.phase = "honeypot";
        out.module = BT_MOD_PREDATOR;
        out.lineCount = 0;
        out.lines[out.lineCount++] = "identity: " + s_targetName;
        out.lines[out.lineCount++] = bt_fmt("writes captured: %lu", (unsigned long)s_hpWrites);
        return true;
    }
    if (s_reconActive) {
        out.title = "BLE Predator"; out.running = true; out.phase = "recon";
        out.module = BT_MOD_PREDATOR;
        out.lineCount = 0;
        out.lines[out.lineCount++] = bt_fmt("chars found: %u", (unsigned)s_recon.size());
        return true;
    }
    if (s_scanActive && bt_scan_running()) {
        out.title = "BLE Predator"; out.running = true; out.phase = "scan";
        out.module = BT_MOD_PREDATOR;
        out.lineCount = 0;
        int conn = 0;
        for (int i = 0; i < bt_dev_count(); i++) if (bt_dev_get(i)->flags & 0x80) conn++;
        out.lines[out.lineCount++] = bt_fmt("connectable: %d", conn);
        out.lines[out.lineCount++] = bt_fmt("devices: %d", bt_dev_count());
        return true;
    }
    return false;
}

std::vector<BtGattCharInfo>* bt_predator_recon_table() { return &s_recon; }
String bt_predator_target_name() { return s_targetName; }
bool   bt_predator_recon_done()  { return s_reconDone; }

String bt_predator_recon_error() { return s_reconError; }
