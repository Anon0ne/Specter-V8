#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace specter {
constexpr size_t kSessionNameMax = 48;
constexpr const char* kSessionRoot = "/specter/sessions/";
inline bool validSessionName(const char* value, size_t length) {
  if (!value || !length || length > kSessionNameMax) return false;
  bool visible = false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if (c < 32 || c == 127) return false;
    if (c != ' ') visible = true;
  }
  return visible;
}
inline bool validSessionPath(const char* path) {
  if (!path) return false;
  const size_t prefix = strlen(kSessionRoot);
  if (strlen(path) != prefix + 19 || strncmp(path, kSessionRoot, prefix) != 0 ||
      path[prefix] != 's' || path[prefix + 1] != '_' || path[prefix + 10] != '_') return false;
  for (size_t i = prefix + 2; i < prefix + 19; ++i) {
    if (i == prefix + 10) continue;
    const char c = path[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}
inline bool globalSavedList(const char* name) {
  return name && (strcmp(name, "APs") == 0 || strcmp(name, "SSIDs") == 0 || strcmp(name, "Airtags") == 0);
}
inline bool validOutputCategory(const char* category) {
  return category && (strcmp(category, "captures") == 0 || strcmp(category, "reports") == 0 ||
                      strcmp(category, "recon") == 0 || strcmp(category, "bluetooth") == 0);
}
inline bool parseElapsedMs(const char* text, uint64_t& value) {
  if (!text || !*text) return false;
  uint64_t parsed = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    const unsigned digit = *p - '0';
    if (parsed > (UINT64_MAX - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }
  value = parsed;
  return true;
}

}
