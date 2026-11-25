// hale_features.h — MarauderHale feature flags
//
// HaleHound-inspired modules for the ESP32 Marauder, scoped to the Marauder V8's
// OWN radios: ESP32 built-in WiFi + BLE (2.4 GHz). No external CC1101 / NRF24 / PN532.
//
// Flags are enabled as modules are implemented and verified.
// Offensive modules must additionally check the VALHALLA gate at runtime
// (see hale_valhalla / Blue Team mode) before transmitting.
//
// Provenance: features are reimplemented clean-room from HaleHound's public README
// and open sources (ESP32-DIV MIT, public CVE research). No HaleHound binary is used.
// See THIRD_PARTY_NOTICE.md.

#pragma once

#define HALE_FW_NAME     "MarauderHale"
#define HALE_FW_VERSION  "0.1.0-dev"

// ----- Hardware scope guard -------------------------------------------------
// We intentionally do NOT support external radios. These stay 0.
#define HALE_HAS_CC1101  0   // SubGHz        — out of scope (no hardware)
#define HALE_HAS_NRF24   0   // 2.4GHz/NRF24  — out of scope (no hardware)
#define HALE_HAS_PN532   0   // NFC/RFID      — out of scope (no hardware)

// ----- Phase 1: safety ------------------------------------------------------
#define HALE_TEAM_MODE        1   // Red Team / Blue Team: gate offensive (TX) modules (default Blue)

// ----- Phase 2: BLE recon & exploit suite (ESP32 built-in BLE) --------------
#define HALE_BLE_PREDATOR     0   // GATT recon -> connectable honeypot + loot
#define HALE_BLE_SKELETONKEY  0   // GATT enumerate + writable-characteristic fuzz
#define HALE_BLE_WHISPERPAIR  0   // CVE-2025-36911 Fast Pair probe
#define HALE_BLE_AIROHA_RACE  0   // CVE-2025-20700/1/2 link-key extraction
#define HALE_BLE_KARR         0   // SWDS BLE car-alarm detect + assess (read-only)
#define HALE_BLE_AIRTAG_PLUS  0   // AirTag Replay + Scream (extends FindMy)
#define HALE_BLE_SKIMMER      0   // BLE card-skimmer hunter (BETA)

// ----- Phase 3: WiFi UX parity (ESP32 built-in WiFi) ------------------------
#define HALE_TAP_DEAUTH       1   // Scan AP -> tap -> Deauth / Clone (Evil Twin)
#define HALE_WIFI_PROBE_TWIN  1   // probe sniff -> auto Evil Twin
#define HALE_WIFI_GARMR       0   // captive-portal cred-harvest polish
#define HALE_WIFI_AUTHFLOOD   0   // auth-flood variant + activity visualizer

// ----- Phase 1b: Deauth Guardian (Blue Team) — user priority ----------------
#define HALE_DEAUTH_GUARDIAN  1   // deauth/disassoc flood detection + attribution + alert
// Tunable detection defaults (frames/sec over the sliding window):
#define HALE_DG_SUSPECT_PPS   3    // >= this rate -> SUSPECT
#define HALE_DG_ATTACK_PPS    10   // >= this rate -> UNDER ATTACK
#define HALE_DG_WINDOW_MS     5000 // sliding window length
#define HALE_DG_COOLDOWN_MS   3000 // must stay calm this long before downgrading to SECURE

// ----- Phase 4: SIGINT extras ----------------------------------------------
#define HALE_IOT_RECON        1   // scoped service discovery + SD report
#define HALE_LOOT_BROWSER     0   // unified categorized loot browser
// (WiFi Guardian deauth-detection folded into HALE_DEAUTH_GUARDIAN above)

// ----- Phase 5: drone extras ------------------------------------------------
#define HALE_DRONE_RID_SPOOF  0   // broadcast ASTM F3411 Remote ID (WiFi+BLE)
#define HALE_DRONE_DEAUTH     0   // targeted deauth of a drone control link
