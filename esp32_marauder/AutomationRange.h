#pragma once
#include "IPv4Range.h"
namespace marauder {
inline bool validAutomationRange(uint32_t first, uint32_t last,
                                 uint32_t self, uint32_t mask) {
  const IPv4HostRange range = ipv4HostRange(self, mask);
  return range.valid && last >= first && uint64_t(last) - first + 1 <= 254 &&
         first >= range.first && last <= range.last;
}
}
