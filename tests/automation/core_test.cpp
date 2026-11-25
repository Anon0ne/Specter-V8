#include "UtcTime.h"
#include "ProbeMatch.h"
#include "AutomationRange.h"
#include <assert.h>
#include <iostream>
#include <string>
int main() {
  assert(marauder::validAutomationRange(0xC0A80101, 0xC0A801FE, 0xC0A80110, 0xFFFFFF00));
  assert(!marauder::validAutomationRange(0xC0A80100, 0xC0A801FE, 0xC0A80110, 0xFFFFFF00));
  assert(!marauder::validAutomationRange(0xC0A80101, 0xC0A801FF, 0xC0A80110, 0xFFFFFF00));
  assert(!marauder::validAutomationRange(0xC0A80201, 0xC0A80202, 0xC0A80110, 0xFFFFFF00));
  assert(!marauder::validAutomationRange(0xC0A80110, 0xC0A80101, 0xC0A80110, 0xFFFFFF00));
  assert(!marauder::validAutomationRange(0xC0A80101, 0xC0A80301, 0xC0A80110, 0xFFFF0000));
  assert(!marauder::validAutomationRange(0xC0A80101, 0xC0A80102, 0xC0A80110, 0xFF00FF00));
  assert(!marauder::validAutomationRange(0xC0A80110, 0xC0A80110, 0xC0A80110, 0xFFFFFFFF));
  uint64_t value = 123;
  assert(marauder::parseUtcDateTime("2024-02-29 12:34:56", value) && value == 1709210096ULL);
  assert(marauder::parseUtcDateTime("2024-02-29T12:34:56Z", value) && value == 1709210096ULL);
  for (const char* invalid : {"", "2024-02-29", "2023-02-29 12:34:56", "2024-02-29 24:00:00",
       "2024/02/29 12:34:56", "2024-02-29 12:34:56+01:00", "2024-02-29 12:34:56x", "2024-02-29T12:34:56ZZ"})
    assert(!marauder::parseUtcDateTime(invalid, value));
  assert(marauder::parseClockEpoch("1577836800", value));
  assert(value == 1577836800ULL);
  assert(marauder::parseClockEpoch("4102444799", value));
  for (const char* bad : {"", "-1", "+1790000000", "1790000000x", "0", "4102444800", "9999999999999999999999999"})
    assert(!marauder::parseClockEpoch(bad, value));
  assert(marauder::utcToEpoch(2020,1,1,0,0,0,value) && value == 1577836800ULL);
  assert(marauder::utcToEpoch(2024,2,29,12,34,56,value) && value == 1709210096ULL);
  assert(!marauder::utcToEpoch(2023,2,29,0,0,0,value));
  assert(!marauder::utcToEpoch(2024,4,31,0,0,0,value));
  assert(!marauder::utcToEpoch(1970,1,1,0,0,0,value));
  assert(!marauder::utcToEpoch(2024,1,1,24,0,0,value));
  assert(!marauder::utcToEpoch(2024,1,1,0,60,0,value));
  assert(!marauder::utcToEpoch(2024,1,1,0,0,60,value));
  uint8_t frame[80] = {}; frame[0]=0x40; frame[24]=0; frame[25]=3;
  memcpy(frame+26,"Lab",3);
  assert(marauder::probeMatches(frame,29,"Lab",3));
  assert(!marauder::probeMatches(frame,28,"Lab",3));
  assert(!marauder::probeMatches(frame,29,"lab",3));
  assert(!marauder::probeMatches(frame,29,"Lab2",4));
  assert(!marauder::probeMatches(frame,29,"",0));
  frame[25]=0; assert(!marauder::probeMatches(frame,26,"Lab",3));
  frame[25]=255; assert(!marauder::probeMatches(frame,29,"Lab",3));
  frame[0]=0x80; frame[25]=3; assert(!marauder::probeMatches(frame,29,"Lab",3));
  frame[0]=0x40; frame[24]=1; frame[25]=1; frame[26]=0;
  frame[27]=0;frame[28]=3;memcpy(frame+29,"Lab",3);
  assert(marauder::probeMatches(frame,32,"Lab",3));
  // Exercise every short/truncated frame length under address/undefined sanitizers.
  for (size_t n=0;n<32;++n) (void)marauder::probeMatches(frame,n,"Lab",3);
  assert(!marauder::probeMatches(nullptr,32,"Lab",3));
  std::cout << "UTC, subnet-range validation, and bounded probe parsing checks passed\n";
}
