// ============================================================================
// bt_phantom.h — Phantom Flood: fake Apple FindMy advertisements (OpenTag-
//                style published research) + replay of captured adverts.
// Gated behind `bt arm`.
// ============================================================================
#pragma once
#include "bt_core.h"

void   bt_phantom_loop();
bool   bt_phantom_command(const String& c);
void   bt_phantom_stop();
bool   bt_phantom_status(BtUiState& out);

// used by bt_predator honeypot to check radio conflicts; may be NULL-safe
bool   bt_phantom_running();
