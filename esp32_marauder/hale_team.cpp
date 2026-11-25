// hale_team.cpp — see hale_team.h
#include "hale_team.h"

#if HALE_TEAM_MODE

#include <Preferences.h>

namespace hale {

static bool     s_loaded = false;
static TeamMode s_mode   = TEAM_BLUE;  // safe default: offensive locked

static void ensureLoaded() {
  if (s_loaded) return;
  Preferences p;
  if (p.begin("haleteam", /*readOnly=*/true)) {
    s_mode = (p.getUChar("mode", TEAM_BLUE) == TEAM_RED) ? TEAM_RED : TEAM_BLUE;
    p.end();
  }
  s_loaded = true;
}

TeamMode getTeamMode() {
  ensureLoaded();
  return s_mode;
}

void setTeamMode(TeamMode m) {
  s_mode   = (m == TEAM_RED) ? TEAM_RED : TEAM_BLUE;
  s_loaded = true;
  Preferences p;
  if (p.begin("haleteam", /*readOnly=*/false)) {
    p.putUChar("mode", (uint8_t)s_mode);
    p.end();
  }
}

bool offensiveAllowed() { return getTeamMode() == TEAM_RED; }

const char* teamModeName() { return getTeamMode() == TEAM_RED ? "RED TEAM" : "BLUE TEAM"; }

}  // namespace hale

#endif  // HALE_TEAM_MODE
