#pragma once
#include <stdint.h>
#include <stddef.h>

namespace marauder {
constexpr uint64_t MIN_CLOCK_EPOCH = 1577836800ULL; // 2020-01-01 UTC
constexpr uint64_t MAX_CLOCK_EPOCH = 4102444799ULL; // 2099-12-31 UTC
inline bool parseClockEpoch(const char* text, uint64_t& value) {
  if (!text || !*text) return false;
  uint64_t parsed = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    const unsigned digit = *p - '0';
    if (parsed > (MAX_CLOCK_EPOCH - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }
  if (parsed < MIN_CLOCK_EPOCH) return false;
  value = parsed;
  return true;
}
inline bool utcToEpoch(int year, int month, int day, int hour, int minute,
                       int second, uint64_t& epoch) {
  if (year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
    return false;
  const int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (day > daysInMonth[month - 1] + (month == 2 && leap ? 1 : 0)) return false;
  uint64_t days = 0;
  for (int y = 1970; y < year; ++y)
    days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  for (int m = 1; m < month; ++m)
    days += daysInMonth[m - 1] + (m == 2 && leap ? 1 : 0);
  epoch = ((days + day - 1) * 24 + hour) * 3600 + minute * 60 + second;
  return true;
}
// Manual input is UTC and uses YYYY-MM-DD HH:MM:SS (or an ISO T separator).
inline bool parseUtcDateTime(const char* text, uint64_t& epoch) {
  if (!text) return false;
  size_t size = 0;
  while (size < 21 && text[size]) ++size;
  if (size != 19 && !(size == 20 && text[19] == 'Z')) return false;
  for (size_t i = 0; i < 19; ++i) {
    if (i == 4 || i == 7) { if (text[i] != '-') return false; }
    else if (i == 10) { if (text[i] != ' ' && text[i] != 'T') return false; }
    else if (i == 13 || i == 16) { if (text[i] != ':') return false; }
    else if (text[i] < '0' || text[i] > '9') return false;
  }
  const auto pair = [text](size_t i) { return (text[i]-'0')*10 + text[i+1]-'0'; };
  const int year = pair(0)*100 + pair(2);
  return utcToEpoch(year, pair(5), pair(8), pair(11), pair(14), pair(17), epoch);
}

}
