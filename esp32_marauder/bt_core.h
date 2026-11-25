// ============================================================================
// bt_core.h — shared infrastructure for the Marauder V8 BT+ feature pack
//
// Provides: radio lifecycle, raw-adv TX (core NimBLE host API), a single
// shared scanner with sink dispatch, a device table, JSONL loot logging to
// SD (LittleFS fallback), an authorization gate, the CLI dispatcher and the
// UI aggregation layer the touchscreen menu renders from.
//
// Integration: see INTEGRATION.md — the host only needs to call
//   bt_loop() from the main loop and bt_command(String) from the CLI.
// ============================================================================
#pragma once

#include <Arduino.h>
#include <FS.h>
#include "bt_config.h"

// ------------------------------------------------------------------ status --
enum BtModuleId : uint8_t {
    BT_MOD_NONE = 0, BT_MOD_TRACKER, BT_MOD_PHANTOM, BT_MOD_BEACON,
    BT_MOD_SPOOFER, BT_MOD_PREDATOR, BT_MOD_SKELETON, BT_MOD_WHISPER,
    BT_MOD_KARR, BT_MOD_SKIMMER, BT_MOD_BADBLE
};

struct BtUiState {
    const char* title;                  // screen title, e.g. "Tracker Scan"
    bool        running;                // module actively working
    const char* phase;                  // e.g. "scanning" / "recon" / "idle"
    BtModuleId  module;
    String      lines[BT_UI_MAX_LINES]; // pre-formatted status lines
    uint8_t     lineCount;
};

// Snapshot of the currently active module; false when all modules are idle.
bool bt_ui_get(BtUiState& out);

// ---------------------------------------------------------------- commands --
// Feed the FULL command line (e.g. "tracker scan 30"). Returns true if the
// pack consumed it — caller should not try other handlers.
bool   bt_command(const String& cmd);
String bt_help_text();
bool   bt_stop_all();           // stops every module; CLI: `stopble`

// -------------------------------------------------------------- main loop --
// Drives all module state machines. Non-blocking; call every loop().
void bt_loop();
bool bt_busy();   // true while a BLE scan or TX job is running (for Sessions busy hook)

// ------------------------------------------------------- authorization gate
// Passive detectors run without arming. TX / active modules refuse to start
// until armed once per boot:  "bt arm"  ->  "bt arm yes"  (persisted in NVS).
bool   bt_is_armed();
String bt_arm_begin();          // returns disclaimer text, starts confirm window
bool   bt_arm_confirm();
void   bt_disarm();

// ------------------------------------------------------------------- radio --
void bt_set_radio_hooks(bool (*pre)(void), void (*post)(void));
// pre()  : called before NimBLE init (host should stop WiFi scanning/AP etc.)
// post() : called after NimBLE deinit (host may restore WiFi)
// Both may be NULL. Return value of pre(): true = wifi stopped, false = it
// was already off.

bool bt_radio_begin();          // lazy NimBLE host init
void bt_radio_end();            // deinit host (drops all BLE state)
bool bt_radio_ready();

// ------------------------------------------------------- raw advertisement --
// Transmit one legacy-format advertisement built by a feature module.
// Uses the NimBLE host core API (ble_gap_adv_set_data/ble_gap_adv_start),
// stable across NimBLE-Arduino 1.x and 2.x.
bool bt_tx_raw(const uint8_t* adv, size_t len, uint16_t interval_ms,
               bool rotate_random_addr);
void bt_tx_stop();
bool bt_tx_active();
void bt_tx_tick();              // re-arm advert when the interval elapsed
bool bt_tx_rearm();             // force payload reload next tick
uint32_t bt_tx_repeats();       // adverts actually pushed since last bt_tx_raw

// ------------------------------------------------------------- scanner ------
// One shared scanner. Sinks receive every parsed advertisement frame.
struct bt_dev_t {
    uint8_t  mac[6];
    uint8_t  addrType;                 // 0 = public, 1 = random
    int8_t   rssi;                     // best seen
    uint32_t firstSeen;                // millis()
    uint32_t lastSeen;
    uint16_t hits;
    char     name[BT_NAME_MAX];        // truncated, may be empty
    uint8_t  plen;
    uint8_t  payload[BT_ADV_MAX_LEN];  // last legacy payload (<=31B)
    uint8_t  kind;                     // module-specific classification tag
    uint8_t  flags;                    // module-specific bit flags
};

typedef void (*bt_sink_fn)(const bt_dev_t* dev, const uint8_t* adv,
                           size_t len, int rssi);

void bt_scan_add_sink(bt_sink_fn fn);
void bt_scan_start(uint32_t duration_ms /*0 = until bt_scan_stop*/);
void bt_scan_stop();
bool bt_scan_running();

int       bt_dev_count();
bt_dev_t* bt_dev_get(uint8_t idx);     // NULL if out of range
void      bt_dev_clear();
String    bt_dev_line(uint8_t idx);    // formatted "[idx] AA:BB.. RSSI name"

// ------------------------------------------------------------------ loot ----
// JSONL loot writer. Pass &SD after mounting the card, or nullptr to use
// LittleFS. Files: <root>/<module>/<module>_<seq>.jsonl
void   bt_storage_init(fs::FS* sdFs);
bool   bt_loot_open(const char* module);
void   bt_loot_writeln(const String& jsonl);
void   bt_loot_close();
bool   bt_loot_ready();
String bt_loot_path();
String bt_loot_summary();              // for "bt loot" CLI

// ------------------------------------------------------------------ misc ----
String bt_mac_str(const uint8_t* mac);
String bt_hex_str(const uint8_t* d, size_t n, char sep = 0);
String bt_fmt(const char* fmt, ...);   // printf-style into a String (UI lines)
String bt_json_escape(const String& s);
String bt_ascii_dump(const uint8_t* d, size_t n);   // printable or '.'
bool   bt_parse_hex6(const String& s, uint8_t out[6]);
bool   bt_parse_uuid(const String& s, const char* base128, uint8_t out[16]);
