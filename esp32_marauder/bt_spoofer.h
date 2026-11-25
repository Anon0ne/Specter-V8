// ============================================================================
// bt_spoofer.h — multi-platform BLE pairing spam ("BLE Spoofer")
//  * Apple Continuity "nearby action" messages (29 device roles, protocol
//    type IDs documented in docs/payload_tables.md)
//  * Google Fast Pair (service data 0xFE2C, 3-byte model IDs)
//  * Microsoft Swift Pair (service UUID 0x060D + display name)
// Gated behind `bt arm`.
// ============================================================================
#pragma once
#include "bt_core.h"

void   bt_spoofer_loop();
bool   bt_spoofer_command(const String& c);
void   bt_spoofer_stop();
bool   bt_spoofer_status(BtUiState& out);
