// ============================================================================
// bt_skimmer.h — Skimmer Hunt (BETA): passive BLE card-skimmer detector.
// Multi-signal scoring engine; optional arm-gated active confirmation.
// ============================================================================
#pragma once
#include "bt_core.h"

void   bt_skimmer_register();
void   bt_skimmer_loop();
bool   bt_skimmer_command(const String& c);
void   bt_skimmer_stop();
bool   bt_skimmer_status(BtUiState& out);
