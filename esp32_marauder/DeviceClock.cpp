#include "DeviceClock.h"
#include "UtcTime.h"
#include "configs.h"
#include <sys/time.h>
#include <utime.h>
#ifdef HAS_SD
#include "SDInterface.h"
extern SDInterface sd_obj;
#endif

namespace marauder {
static const char* clockSource = "unknown";
bool clockValid() {
  const time_t now = time(nullptr);
  return now >= static_cast<time_t>(MIN_CLOCK_EPOCH) &&
         static_cast<uint64_t>(now) <= MAX_CLOCK_EPOCH;
}
bool setClockUtc(uint64_t epoch, const char* source) {
  if (epoch < MIN_CLOCK_EPOCH || epoch > MAX_CLOCK_EPOCH) return false;
  timeval value = {};
  value.tv_sec = static_cast<time_t>(epoch);
  if (static_cast<uint64_t>(value.tv_sec) != epoch || settimeofday(&value, nullptr) != 0)
    return false;
  setenv("TZ", "UTC0", 1);
  tzset();
  clockSource = source;
  return true;
}
String clockTimeSource() { return clockValid() ? String(clockSource) : String("unsynced"); }
String clockUtc() { return clockValid() ? formatClockUtc(time(nullptr)) : String("unsynced"); }
String formatClockUtc(time_t now) {
  if (now < static_cast<time_t>(MIN_CLOCK_EPOCH) || static_cast<uint64_t>(now) > MAX_CLOCK_EPOCH)
    return "unknown/unset";
  tm utc = {};
  gmtime_r(&now, &utc);
  char text[24];
  strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(text);
}
bool setFileDateUtc(const String& path, uint64_t epoch, String& error) {
  error = "";
  if (epoch < MIN_CLOCK_EPOCH || epoch > MAX_CLOCK_EPOCH || !path.startsWith("/") ||
      path.indexOf("..") >= 0 || path.indexOf('\\') >= 0 ||
      path.indexOf('\n') >= 0 || path.indexOf('\r') >= 0) {
    error = "Invalid date or SD path"; return false;
  }
  #ifdef HAS_SD
  if (!sd_obj.supported) { error = "SD unavailable"; return false; }
  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    error = "Select an existing file"; return false;
  }
  file.close();
  setenv("TZ", "UTC0", 1);
  tzset();
  utimbuf times = {};
  times.actime = times.modtime = static_cast<time_t>(epoch);
  if (static_cast<uint64_t>(times.modtime) != epoch ||
      utime((String("/sd") + path).c_str(), &times) != 0) {
    error = "Timestamp update failed"; return false;
  }
  return true;
  #else
  error = "SD not supported"; return false;
  #endif
}
bool testSdClock(String& result) {
  #ifdef HAS_SD
  if (!clockValid()) { result = "Set UTC or obtain GPS time first"; return false; }
  if (!sd_obj.supported) { result = "SD unavailable"; return false; }
  const String path = "/clock_test_" + String(millis()) + ".tmp";
  if (SD.exists(path)) { result = "Test filename in use; retry"; return false; }
  const time_t started = time(nullptr);
  const String contents = "MarauderHale SD timestamp test\n";
  File file = SD.open(path, FILE_WRITE);
  if (!file) { result = "Cannot create SD test file"; return false; }
  bool okay = file.print(contents) == contents.length();
  file.flush(); file.close();
  file = SD.open(path, FILE_READ);
  time_t created = 0;
  if (file) {
    created = file.getLastWrite();
    okay = okay && file.readString() == contents &&
      created >= started - 2 && created <= time(nullptr) + 2;
    file.close();
  } else okay = false;
  const uint64_t known = static_cast<uint64_t>(started) >= MIN_CLOCK_EPOCH + 3600
      ? static_cast<uint64_t>(started) - 3600 : static_cast<uint64_t>(started);
  String error;
  if (okay) okay = setFileDateUtc(path, known, error);
  time_t modified = 0;
  if (okay) {
    file = SD.open(path, FILE_READ);
    if (file) {
      modified = file.getLastWrite();
      const int64_t difference = static_cast<int64_t>(modified) - static_cast<int64_t>(known);
      okay = difference >= -2 && difference <= 2 && file.readString() == contents;
      file.close();
    } else okay = false;
  }
  const bool removed = SD.remove(path);
  result = okay ? "SD dates verified; test file removed" : "SD timestamp test FAILED";
  if (!removed) { result += "; test file remains"; okay = false; }
  Serial.printf("[clock] sdtest %s created=%lld modified=%lld target=%llu removed=%u\n",
    okay ? "PASS" : "FAIL", static_cast<long long>(created), static_cast<long long>(modified),
    static_cast<unsigned long long>(known), removed ? 1 : 0);
  return okay;
  #else
  result = "SD not supported"; return false;
  #endif
}
bool handleClockCommand(const String& input) {
  if (input == "clock sdtest") {
    String result;
    const bool okay = testSdClock(result);
    Serial.println(String(okay ? "[clock] " : "[clock] ERROR ") + result);
    return true;
  }
  if (input == "clock" || input == "clock status") {
    Serial.printf("[clock] utc=%s source=%s epoch=%llu\n", clockUtc().c_str(),
                  clockValid() ? clockSource : "unsynced",
                  static_cast<unsigned long long>(time(nullptr)));
    return true;
  }
  if (input.startsWith("clock set ")) {
    uint64_t epoch = 0;
    if (!parseClockEpoch(input.substring(10).c_str(), epoch) || !setClockUtc(epoch, "host"))
      Serial.println(F("[clock] ERROR expected Unix UTC seconds in 2020..2099"));
    else Serial.println("[clock] synced utc=" + clockUtc());
    return true;
  }
  if (input == "clock" || input.startsWith("clock ")) {
    Serial.println(F("Usage: clock [status | set <unix_utc_seconds> | sdtest]"));
    return true;
  }
  if (input.startsWith("filedate ")) {
    const int split = input.indexOf(' ', 9);
    uint64_t epoch = 0;
    if (split < 0 || !parseClockEpoch(input.substring(9, split).c_str(), epoch)) {
      Serial.println(F("Usage: filedate <unix_utc_seconds> </existing/file>")); return true;
    }
    const String path = input.substring(split + 1);
    String error;
    if (!setFileDateUtc(path, epoch, error)) Serial.println("[filedate] ERROR " + error);
    else Serial.println("[filedate] updated " + path);
    return true;
  }
  return false;
}
}
