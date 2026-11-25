#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace marauder {
// Frame length excludes FCS. Reject truncated IEs and wildcard/hidden SSIDs.
inline bool probeMatches(const uint8_t* frame, size_t length,
                         const char* ssid, size_t ssidLength) {
  if (!frame || !ssid || !ssidLength || ssidLength > 32 || length < 26 ||
      (frame[0] & 0xFC) != 0x40) return false;
  size_t offset = 24;
  while (offset + 2 <= length) {
    const uint8_t tag = frame[offset++];
    const size_t size = frame[offset++];
    if (size > length - offset) return false;
    if (tag == 0) return size == ssidLength && memcmp(frame + offset, ssid, size) == 0;
    offset += size;
  }
  return false;
}
}
