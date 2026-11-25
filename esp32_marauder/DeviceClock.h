#pragma once
#include <Arduino.h>
#include <time.h>
namespace marauder {
bool clockValid();
bool setClockUtc(uint64_t epoch, const char* source);
String clockUtc();
String clockTimeSource();
String formatClockUtc(time_t epoch);
bool setFileDateUtc(const String& path, uint64_t epoch, String& error);
bool testSdClock(String& result);
bool handleClockCommand(const String& input);
}
