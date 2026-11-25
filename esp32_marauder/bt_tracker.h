// ============================================================================
// bt_tracker.h — multi-platform BLE tracker scanner (HaleHound "Tracker Scan"
//               + "AirTag Detect" equivalent)
// ============================================================================
#pragma once
#include "bt_core.h"

// classification tags stored in bt_dev_t.kind
enum BtTrackerKind : uint8_t {
    BT_TRK_UNKNOWN = 0,
    BT_TRK_AIRTAG,          // Apple FindMy item (AirTag-like)
    BT_TRK_FINDMY_OTHER,    // other FindMy accessory (headphones, 3rd party)
    BT_TRK_SMARTTAG,        // Samsung SmartTag (heuristic)
    BT_TRK_TILE,            // Tile
    BT_TRK_FMDN,            // Google Find Hub / Find My Device Network
    BT_TRK_CHIPOLO_FINDMY   // Chipolo ONE Spot etc. (FindMy-based)
};

struct BtTrackerStats {
    uint16_t airtags;
    uint16_t findmyOther;
    uint16_t smarttags;
    uint16_t tiles;
    uint16_t fmdn;
    uint16_t total;
    uint32_t frames;      // adv frames processed
};

void   bt_tracker_register();              // attaches scan sink
bool   bt_tracker_command(const String& c);
void   bt_tracker_loop();
void   bt_tracker_stop();
bool   bt_tracker_status(BtUiState& out);  // false when idle
String bt_tracker_kind_name(uint8_t kind);
BtTrackerStats* bt_tracker_stats();
