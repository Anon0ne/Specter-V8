// ============================================================================
// bt_config.h — compile-time knobs for the BT+ feature pack
// Target: Marauder V8 (ESP32-C5, Arduino core 3.x, NimBLE-Arduino 2.x)
// ============================================================================
#pragma once

// ------------------------- device / table limits ---------------------------
#define BT_MAX_DEVS          64     // shared device table size (scan results)
#define BT_UI_MAX_LINES      8      // max status lines exposed to the GUI
#define BT_NAME_MAX          24     // max stored device name length
#define BT_ADV_MAX_LEN       31     // legacy advertisement payload cap
#define BT_PAYLOAD_KEY_LEN   22     // FindMy public key length

// ------------------------------- storage -----------------------------------
#define BT_LOOT_ROOT_SD      "/btloot"   // on SD card (fs::FS* injected by host)
#define BT_LOOT_ROOT_FLASH   "/btloot"   // LittleFS fallback
#define BT_LOOT_FLUSH_MS     1500        // buffered write flush interval

// ------------------------------ spam timing --------------------------------
#define BT_SPOOF_INTERVAL_MS 100    // default ms between advertisement rotates
#define BT_PHANTOM_ROTATE_N  25     // rotate FindMy pubkey every N advert sets
#define BT_PHANTOM_MAC_ROT   8      // rotate random MAC every N key rotations
#define BT_BEACON_INTERVAL_MS 200   // default beacon interval

// ------------------------------ scanner tuning ------------------------------
#define BT_SCAN_INTERVAL     45     // NimBLE scan interval (units of 0.625 ms)
#define BT_SCAN_WINDOW       30     // NimBLE scan window
#define BT_SCAN_ACTIVE       true   // active scanning (ask for scan rsp)

// --------------------------- client / recon limits --------------------------
#define BT_CONN_TIMEOUT_MS   4000   // GATT client connect timeout
#define BT_MAX_RECON_SVCS    20     // cap services enumerated per device
#define BT_MAX_RECON_CHARS   60     // cap characteristics enumerated
#define BT_HONEYPOT_SVCS     8      // max services mirrored into the honeypot
#define BT_HONEYPOT_CHARS    16     // max characteristics mirrored
#define BT_FUZZ_STEP_MS      120    // delay between fuzz writes
#define BT_FUZZ_MAX_WRITES   200    // hard cap per fuzz run

// ---------------------------- authorization gate ----------------------------
// TX/active modules require `bt arm` once per boot (persisted in NVS).
// Passive detectors (tracker / karr / skimmer / predator scan) run un-armed.
#define BT_ARM_NVS_NS        "btpack"
#define BT_ARM_NVS_KEY       "armed"

// ---------------------------- feature switches ------------------------------
#define BT_ENABLE_TRACKER    1
#define BT_ENABLE_PHANTOM    1
#define BT_ENABLE_BEACON     1
#define BT_ENABLE_SPOOFER    1
#define BT_ENABLE_PREDATOR   1
#define BT_ENABLE_SKELETON   1
#define BT_ENABLE_WHISPER    1
#define BT_ENABLE_KARR       1
#define BT_ENABLE_SKIMMER    1
