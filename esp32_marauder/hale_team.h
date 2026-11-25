// hale_team.h — Red Team / Blue Team mode for MarauderHale.
//
// Blue Team (default): passive/defensive modules only — scanners, sniffers,
// the Deauth Guardian detection, wardriving, GPS. Offensive (transmitting)
// modules are blocked.
// Red Team: offensive modules unlocked (authorized testing only).
//
// The choice is persisted in NVS (ESP32 Preferences) and survives reboots.
// Enforcement is a single check in WiFiScan::StartScan(); this module only
// owns the mode value so it stays dependency-light.

#pragma once

#include "hale_features.h"

#if HALE_TEAM_MODE

#include <stdint.h>

namespace hale {

enum TeamMode : uint8_t { TEAM_BLUE = 0, TEAM_RED = 1 };

TeamMode    getTeamMode();          // cached read (loads from NVS once)
void        setTeamMode(TeamMode m);// persists to NVS + updates cache
bool        offensiveAllowed();     // true only in Red Team
const char* teamModeName();         // "RED TEAM" / "BLUE TEAM"

}  // namespace hale

#endif  // HALE_TEAM_MODE
