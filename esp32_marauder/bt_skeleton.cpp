// ============================================================================
// bt_skeleton.cpp — SkeletonKey
//
// fuzz payloads (classic BLE robustness probes):
//   0x00 fill, 0xFF fill, 0xAA, 0x55, length walk 1..N, "0123456789ABCDEF",
//   random fill. After each write to a readable characteristic the value is
//   read back and logged. Every write is JSONL-logged to SD.
//
// LED presets implement the widely documented Magic Home / Floralert "0xFFE9"
// control characteristic protocol:
//   on    = 56 FF FF FF 00 F0 AA
//   off   = CC 24 33
//   color = 56 <R> <G> <B> 00 F0 AA
// ============================================================================
#include "bt_skeleton.h"
#include "bt_predator.h"
#include "bt_compat.h"

static NimBLEClient* s_cli = nullptr;
static bool     s_connected = false;
static bool     s_fuzzing   = false;
static uint8_t  s_slot      = 0xFF;
static uint32_t s_fuzzCount = 0;         // writes per char, 0 = default 8
static uint32_t s_writes    = 0;
static uint32_t s_nextStepMs = 0;
static size_t   s_step       = 0;
static bool     s_hasRecon   = false;    // recon table came from predator

// fuzz target list resolved after connect
struct FuzzTarget { uint16_t handle; NimBLERemoteCharacteristic* chr; };
static std::vector<FuzzTarget> s_targets;
static size_t s_tgtIdx = 0;
static size_t s_payloadIdx = 0;

static const uint8_t PAY_00[] = {0x00};
static const uint8_t PAY_FF[] = {0xFF};
static const uint8_t PAY_AA[] = {0xAA};
static const uint8_t PAY_55[] = {0x55};
static const uint8_t PAY_ABC[] = "0123456789ABCDEF";
static const uint8_t PAY_256[32] = {0};   // 32B zero block

// ---------------------------------------------------------------- connect --
static bool sk_connect(uint8_t slot) {
    bt_dev_t* d = bt_dev_get(slot);
    if (!d) { Serial.println("[sk] bad index"); return false; }
    if (bt_scan_running()) bt_scan_stop();
    bt_tx_stop();

    s_slot = slot;
    NimBLEAddress addr(d->mac, d->addrType);
    s_cli = NimBLEDevice::createClient();
    if (!s_cli) return false;
    s_cli->setConnectTimeout(BT_CONN_TIMEOUT_MS / 1000);
    Serial.printf("[sk] connecting %s ...\n", bt_mac_str(d->mac).c_str());
    if (!s_cli->connect(addr)) {
        Serial.println("[sk] connect failed");
        NimBLEDevice::deleteClient(s_cli);
        s_cli = nullptr;
        return false;
    }
    s_connected = true;
    Serial.println("[sk] connected");
    return true;
}

static void sk_disconnect() {
    if (s_cli) { s_cli->disconnect(); NimBLEDevice::deleteClient(s_cli); s_cli = nullptr; }
    s_connected = false;
    s_fuzzing = false;
    s_targets.clear();
}

// ------------------------------------------------------------------- dump --
static bool sk_dump() {
    if (!s_cli) return false;
    if (!bt_loot_ready()) bt_loot_open("skeleton");
    s_hasRecon = false;
    s_targets.clear();

    std::vector<NimBLERemoteService*> svcs = s_cli->getServices(true);
    Serial.printf("[sk] %u services\n", (unsigned)svcs.size());
    for (NimBLERemoteService* svc : svcs) {
        NimBLEUUID su = svc->getUUID();
        String sHex = bt_hex_str(su.getValue(), su.bitSize()/8);
        Serial.printf("  SVC %s\n", sHex.c_str());
        std::vector<NimBLERemoteCharacteristic*> chrs = svc->getCharacteristics(true);
        for (NimBLERemoteCharacteristic* ch : chrs) {
            uint8_t props = bt_chr_props(ch);
            String cHex = bt_hex_str(ch->getUUID().getValue(), ch->getUUID().bitSize()/8);
            String val = "";
            if (props & BLE_GATT_CHR_F_READ) {
                NimBLEAttValue v = ch->readValue();
                val = bt_hex_str((const uint8_t*)v.data(), v.size());
            }
            Serial.printf("    CHR %s p=%02X %s\n", cHex.c_str(), props, val.c_str());
            bt_loot_writeln("{\"ts\":" + String(millis()) +
                            ",\"module\":\"skeleton\",\"type\":\"gatt\"" +
                            ",\"svc\":\"" + sHex + "\",\"chr\":\"" + cHex +
                            "\",\"props\":" + String(props) +
                            ",\"value\":\"" + val + "\"}");
            if (props & (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NR)) {
                FuzzTarget t; t.handle = ch->getHandle(); t.chr = ch;
                s_targets.push_back(t);
            }
        }
    }
    bt_loot_close();
    Serial.printf("[sk] %u writable characteristics ready for fuzz\n",
                  (unsigned)s_targets.size());
    return true;
}

// ------------------------------------------------------------------- fuzz --
static bool write_payload(NimBLERemoteCharacteristic* ch, const uint8_t* p,
                          size_t n) {
    bool ok = ch->writeValue(p, n, false);   // write-without-response default
    s_writes++;
    String hex = bt_hex_str(p, n);
    NimBLEAttValue back;
    bool readBack = false;
    if (bt_chr_props(ch) & BLE_GATT_CHR_F_READ) {
        if (ok) {
            NimBLEAttValue v = ch->readValue();
            back = v; readBack = true;
        }
    }
    Serial.printf("[sk][fuzz] h=%03u %s %s%s%s\n", ch->getHandle(), hex.c_str(),
                  ok ? "->ok" : "->FAIL",
                  readBack ? " back=" : "",
                  readBack ? bt_hex_str((const uint8_t*)back.data(), back.size()).c_str() : "");
    if (bt_loot_ready()) {
        bt_loot_writeln("{\"ts\":" + String(millis()) +
                        ",\"module\":\"skeleton\",\"type\":\"fuzz\"" +
                        ",\"chr_handle\":" + String(ch->getHandle()) +
                        ",\"hex\":\"" + hex + "\",\"ok\":" + (ok ? "true" : "false") + "}");
    }
    return ok;
}

static bool fuzz_step() {                    // one payload per char per call
    if (s_tgtIdx >= s_targets.size()) return false;
    NimBLERemoteCharacteristic* ch = s_targets[s_tgtIdx].chr;
    uint8_t buf[64];
    size_t n = 1;
    switch (s_payloadIdx) {
        case 0: memcpy(buf, PAY_00, 1); n = 1; break;
        case 1: memset(buf, 0xFF, sizeof(buf)); n = sizeof(buf); break;
        case 2: buf[0] = 0xAA; n = 1; break;
        case 3: buf[0] = 0x55; n = 1; break;
        case 4: memcpy(buf, PAY_ABC, sizeof(PAY_ABC) - 1); n = sizeof(PAY_ABC) - 1; break;
        case 5: memset(buf, 0x00, sizeof(PAY_256)); n = sizeof(PAY_256); break;
        default: {
            for (size_t i = 0; i < sizeof(buf); i++) buf[i] = random(256);
            n = sizeof(buf);
            break;
        }
    }
    write_payload(ch, buf, n);
    s_payloadIdx++;
    if (s_payloadIdx >= 7) {                 // 7 payload kinds per char
        s_payloadIdx = 0;
        s_tgtIdx++;
        uint32_t cap = s_fuzzCount ? s_fuzzCount : 8;
        if (s_writes > cap * s_targets.size()) { Serial.println("[sk] fuzz cap reached"); return false; }
    }
    if (s_tgtIdx >= s_targets.size()) return false;
    return true;
}

// ---------------------------------------------------------------- led cfg --
static bool sk_led(const String& arg) {
    if (!s_connected || !s_cli) { Serial.println("[sk] not connected"); return true; }
    // find Magic Home 0xFFE9 characteristic under 0xFFD5-ish services
    NimBLERemoteCharacteristic* target = nullptr;
    std::vector<NimBLERemoteService*> svcs = s_cli->getServices(false);
    for (NimBLERemoteService* svc : svcs) {
        NimBLEUUID su = svc->getUUID();
        if (su.bitSize() != 16) continue;
        uint16_t v = *(uint16_t*)su.getValue();
        if (v == 0xFFD0 || v == 0xFFE0 || v == 0xFFF0 || v == 0xFFF3) {
            std::vector<NimBLERemoteCharacteristic*> chrs = svc->getCharacteristics(false);
            for (NimBLERemoteCharacteristic* ch : chrs) {
                NimBLEUUID cu = ch->getUUID();
                if (cu.bitSize() == 16) {
                    uint16_t cv = *(uint16_t*)cu.getValue();
                    if (cv == 0xFFE9 || cv == 0xFFE1 || cv == 0xFFF3 ||
                        cv == 0xFFD5) { target = ch; break; }
                }
            }
        }
        if (target) break;
    }
    if (!target) { Serial.println("[sk] no known LED control characteristic found"); return true; }

    uint8_t cmd[7];
    if (arg == "on") {
        cmd[0]=0x56; cmd[1]=0xFF; cmd[2]=0xFF; cmd[3]=0xFF; cmd[4]=0x00; cmd[5]=0xF0; cmd[6]=0xAA;
    } else if (arg == "off") {
        cmd[0]=0xCC; cmd[1]=0x24; cmd[2]=0x33;
        write_payload(target, cmd, 3);
        return true;
    } else if (arg.length() == 6) {
        uint8_t r = (uint8_t)strtol(arg.substring(0, 2).c_str(), nullptr, 16);
        uint8_t g = (uint8_t)strtol(arg.substring(2, 4).c_str(), nullptr, 16);
        uint8_t b = (uint8_t)strtol(arg.substring(4, 6).c_str(), nullptr, 16);
        cmd[0]=0x56; cmd[1]=r; cmd[2]=g; cmd[3]=b; cmd[4]=0x00; cmd[5]=0xF0; cmd[6]=0xAA;
    } else {
        Serial.println("usage: sk led on | sk led off | sk led <RRGGBB>");
        return true;
    }
    write_payload(target, cmd, 7);
    return true;
}

// ---------------------------------------------------------------- command --
bool bt_skeleton_command(const String& c) {
    if (!(c == "sk" || c.startsWith("sk "))) return false;
    String a = (c.length() > 3) ? c.substring(3) : "";
    a.trim();

    if (a.startsWith("connect")) {
        if (!bt_is_armed()) { Serial.println("[sk] refused — run `bt arm` first"); return true; }
        int idx = a.substring(7).toInt();
        if (!sk_connect(idx)) return true;
        sk_dump();
        return true;
    }
    if (a.startsWith("dump")) {
        if (!s_connected) { Serial.println("[sk] not connected — sk connect <idx>"); return true; }
        sk_dump();
        return true;
    }
    if (a.startsWith("fuzz")) {
        if (!s_connected) { Serial.println("[sk] not connected"); return true; }
        String r = a.substring(4); r.trim();
        if (r.startsWith("all")) {
            String n = r.substring(3); n.trim();
            s_fuzzCount = n.length() ? (uint32_t)n.toInt() : 0;
        } else if (r.length()) {
            // fuzz <svcHex16> <chrHex16> [n] — resolve specific char
            int sp = r.indexOf(' ');
            if (sp < 0) { Serial.println("usage: sk fuzz all [n] | sk fuzz <svcHex> <chrHex> [n]"); return true; }
            String svcH = r.substring(0, sp), rest = r.substring(sp + 1); rest.trim();
            int sp2 = rest.indexOf(' ');
            String chrH = (sp2 < 0) ? rest : rest.substring(0, sp2);
            s_fuzzCount = (sp2 < 0) ? 0 : (uint32_t)rest.substring(sp2 + 1).toInt();
            s_targets.clear();
            std::vector<NimBLERemoteService*> svcs = s_cli->getServices(false);
            for (NimBLERemoteService* svc : svcs) {
                char sb[5]; snprintf(sb, sizeof(sb), "%04X", *(uint16_t*)svc->getUUID().getValue());
                if (svcH.equalsIgnoreCase(sb)) {
                    std::vector<NimBLERemoteCharacteristic*> chrs = svc->getCharacteristics(false);
                    for (NimBLERemoteCharacteristic* ch : chrs) {
                        char cb[5]; snprintf(cb, sizeof(cb), "%04X", *(uint16_t*)ch->getUUID().getValue());
                        if (chrH.equalsIgnoreCase(cb)) {
                            FuzzTarget t; t.handle = ch->getHandle(); t.chr = ch;
                            s_targets.push_back(t);
                        }
                    }
                }
            }
            if (s_targets.empty()) { Serial.println("[sk] characteristic not found"); return true; }
        }
        s_tgtIdx = 0; s_payloadIdx = 0; s_fuzzing = true; s_nextStepMs = 0;
        if (!bt_loot_ready()) bt_loot_open("skeleton");
        Serial.printf("[sk] fuzzing %u writable chars\n", (unsigned)s_targets.size());
        return true;
    }
    if (a.startsWith("led")) { sk_led(a.substring(3)); a.trim(); return true; }
    if (a == "stop")    { bt_skeleton_stop(); return true; }
    if (a == "disconnect") { sk_disconnect(); Serial.println("[sk] disconnected"); return true; }
    Serial.println("usage: sk connect <idx> | sk dump | sk fuzz all [n] | sk led on|off|<RRGGBB> | sk stop");
    return true;
}

void bt_skeleton_stop() {
    s_fuzzing = false;
    if (s_connected) sk_disconnect();
    bt_loot_close();
}

void bt_skeleton_loop() {
    if (!s_fuzzing) return;
    if (millis() < s_nextStepMs) return;
    s_nextStepMs = millis() + BT_FUZZ_STEP_MS;
    if (!fuzz_step()) {
        Serial.printf("[sk] fuzz done (%lu writes)\n", (unsigned long)s_writes);
        bt_loot_close();
        s_fuzzing = false;
    }
}

bool bt_skeleton_status(BtUiState& out) {
    if (!s_connected && !s_fuzzing) return false;
    out.title = "SkeletonKey";
    out.running = true;
    out.phase = s_fuzzing ? "fuzzing" : "connected";
    out.module = BT_MOD_SKELETON;
    out.lineCount = 0;
    if (s_slot != 0xFF) out.lines[out.lineCount++] = "target: " + bt_dev_line(s_slot);
    out.lines[out.lineCount++] = bt_fmt("writable: %u", (unsigned)s_targets.size());
    out.lines[out.lineCount++] = bt_fmt("writes: %lu", (unsigned long)s_writes);
    return true;
}
