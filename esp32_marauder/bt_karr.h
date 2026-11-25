// ============================================================================
// bt_karr.h — KARR: detect + passively assess BLE car-alarm/aftermarket
// module devices. Verdict-only (no commands are sent to the vehicle).
// Signature table is extensible at runtime — ships minimal and honest.
// ============================================================================
#pragma once
#include "bt_core.h"

void   bt_karr_register();
void   bt_karr_loop();
bool   bt_karr_command(const String& c);
void   bt_karr_stop();
bool   bt_karr_status(BtUiState& out);
