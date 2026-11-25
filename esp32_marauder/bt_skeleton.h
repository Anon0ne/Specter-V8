// ============================================================================
// bt_skeleton.h — SkeletonKey: connect to a BLE device, dump its full GATT
// table, fuzz writable characteristics, drive known LED-strip profiles.
// Reuses the Predator recon table when available. Gated behind `bt arm`.
// ============================================================================
#pragma once
#include "bt_core.h"

void   bt_skeleton_loop();
bool   bt_skeleton_command(const String& c);
void   bt_skeleton_stop();
bool   bt_skeleton_status(BtUiState& out);
