// ============================================================================
// bt_beacon.h — custom iBeacon / Eddystone (UID, URL, TLM) broadcaster
// ============================================================================
#pragma once
#include "bt_core.h"

enum BtBeaconType : uint8_t { BT_BCN_NONE = 0, BT_BCN_IBEACON, BT_BCN_UID,
                              BT_BCN_URL, BT_BCN_TLM, BT_BCN_NAME };

void   bt_beacon_loop();
bool   bt_beacon_command(const String& c);
void   bt_beacon_stop();
bool   bt_beacon_status(BtUiState& out);
