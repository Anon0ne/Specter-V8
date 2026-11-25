#include "SpecterSessionRules.h"
#include <assert.h>
#include <iostream>
#include <string>
int main() {
  using namespace specter;
  assert(validSessionName("Lab 1",5));
  assert(validSessionName("Run \"A\"",7));
  assert(!validSessionName("",0));
  assert(!validSessionName("   ",3));
  assert(!validSessionName("Lab\n1",5));
  assert(!validSessionName(nullptr,10));
  assert(validSessionName(std::string(48,'a').c_str(),48));
  assert(!validSessionName(std::string(49,'a').c_str(),49));
  assert(validSessionPath("/specter/sessions/s_12345678_ABCDEF90"));
  assert(validSessionPath("/specter/sessions/s_00000000_abcdef00"));
  for(const char* path : {"/specter/sessions/../a", "/specter/sessions/s_1_2",
      "/specter/sessions/s_12345678_ABCDEFGH", "/specter/sessions/s_12345678_ABCDEF90/../x",
      "/specter/sessions/s_12345678_ABCDEF90/", "/other/s_12345678_ABCDEF90"})
    assert(!validSessionPath(path));
  assert(!validSessionPath(nullptr));
  assert(globalSavedList("APs")); assert(globalSavedList("SSIDs")); assert(globalSavedList("Airtags"));
  assert(!globalSavedList("wardrive")); assert(!globalSavedList("AP_scan")); assert(!globalSavedList(nullptr));
  assert(validOutputCategory("bluetooth"));
  assert(validOutputCategory("captures"));assert(validOutputCategory("reports"));
  assert(!validOutputCategory("../captures"));assert(!validOutputCategory(nullptr));
  uint64_t elapsed = 0;
  assert(parseElapsedMs("0", elapsed) && elapsed == 0);
  assert(parseElapsedMs("18446744073709551615", elapsed) && elapsed == UINT64_MAX);
  for (const char* bad : {"", "-1", "1ms", "18446744073709551616", "999999999999999999999"})
    assert(!parseElapsedMs(bad, elapsed));
  std::cout << "Session name, storage path and category checks passed\n";
}
