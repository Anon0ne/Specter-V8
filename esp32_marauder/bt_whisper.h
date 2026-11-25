// ============================================================================
// bt_whisper.h — WhisperPair: Google Fast Pair device probe (assessment-only)
// Finds 0xFE2C advertisers, optionally connects and records whether the
// device responds to an unauthenticated Key-Based Pairing request frame.
// Reports a verdict; does NOT complete any pairing. Gated behind `bt arm`.
// ============================================================================
#pragma once
#include "bt_core.h"

void   bt_whisper_register();            // adds scan sink (0xFE2C tagging)
void   bt_whisper_loop();
bool   bt_whisper_command(const String& c);
void   bt_whisper_stop();
bool   bt_whisper_status(BtUiState& out);
