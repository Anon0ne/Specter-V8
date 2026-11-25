#include "SpecterSessions.h"
#include "SpecterSessionRules.h"
#include "SpecterFileIO.h"
#include "DeviceClock.h"
#include "configs.h"
#include "WiFiScan.h"
#include "WifiAutomation.h"
#include "ReconMission.h"
#include "hale_team.h"
#include <ArduinoJson.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <stdio.h>
#include <unistd.h>
#ifdef HAS_SD
#include "SDInterface.h"
extern SDInterface sd_obj;
#endif
extern WiFiScan wifi_scan_obj;
extern ReconMission recon_obj;
extern Buffer buffer_obj;

namespace specter {
namespace {
bool running = false;
SessionInfo current;
uint64_t startedMonotonic = 0;
bool (*externalBusy)() = nullptr;
String millisecondsText(uint64_t value) {
  char text[24]; snprintf(text, sizeof(text), "%llu", (unsigned long long)value); return String(text);
}
uint64_t monotonicMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
bool idleForSessionChange() {
  const marauder::WifiAutomationStatus automation = marauder::wifiAutomationStatus();
  return (!externalBusy || !externalBusy()) && (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF || wifi_scan_obj.currentScanMode == WIFI_CONNECTED) &&
         !recon_obj.active() && !automation.iotRunning && !automation.probeWaiting && !automation.probePortal;
}
#ifdef HAS_SD
bool ensureDirectory(const String& path) {
  if (!sd_obj.supported) return false;
  if (SD.exists(path)) {
    File file = SD.open(path, FILE_READ);
    const bool directory = file && file.isDirectory();
    if (file) file.close();
    return directory;
  }
  return SD.mkdir(path);
}
// Each snapshot has a distinct filename. Publish by rename only after fsync;
// failed completion never overwrites a good start manifest.
bool writeSnapshot(const String& path, const JsonDocument& doc) {
  char text[1536];
  const size_t needed = measureJson(doc);
  if (doc.overflowed() || needed >= sizeof(text) || SD.exists(path)) return false;
  const size_t length = serializeJson(doc, text, sizeof(text));
  const String staging = String("/sd") + path + ".tmp";
  return fileio::publishSnapshot((String("/sd") + path).c_str(), staging.c_str(), text, length);
}
bool readJsonFile(const String& path, JsonDocument& doc) {
  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory() || file.size() > 1536) {
    if (file) file.close();
    return false;
  }
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  return !error;
}
#endif
}

void setExternalBusyHook(bool (*hook)()) { externalBusy = hook; }
bool sessionActive() { return running; }
SessionInfo activeSession() {
  SessionInfo info = current;
  if (running) info.elapsedMs = monotonicMs() - startedMonotonic;
  return info;
}
bool startSession(const String& name, String& error) {
  error = "";
  if (running) { error = "End the active session first"; return false; }
  if (!validSessionName(name.c_str(), name.length())) { error = "Use a name of 1..48 bytes"; return false; }
  if (!idleForSessionChange()) { error = "Stop capture/jobs before starting"; return false; }
  #ifdef HAS_SD
  if (!ensureDirectory("/specter") || !ensureDirectory("/specter/sessions")) {
    error = "SD session folder unavailable"; return false;
  }
  String path;
  bool made = false;
  for (uint8_t attempt = 0; attempt < 8; ++attempt) {
    char id[24];
    snprintf(id, sizeof(id), "s_%08lX_%08lX", (unsigned long)esp_random(), (unsigned long)esp_random());
    path = String(kSessionRoot) + id;
    if (!SD.exists(path) && SD.mkdir(path)) { made = true; break; }
  }
  if (!made) { error = "Cannot create unique session folder"; return false; }
  StaticJsonDocument<1024> doc;
  doc["schema"] = "specter.session.v1";
  doc["project"] = "Specter";
  doc["name"] = name;
  doc["path"] = path;
  doc["started_utc"] = marauder::clockUtc();
  doc["clock_source"] = marauder::clockTimeSource();
  doc["clock_valid"] = marauder::clockValid();
  doc["firmware"] = MARAUDER_VERSION;
  doc["hardware"] = HARDWARE_NAME;
  #if HALE_TEAM_MODE
  doc["team"] = hale::teamModeName();
  #else
  doc["team"] = "not configured";
  #endif
  if (!writeSnapshot(path + "/session.json", doc)) {
    SD.rmdir(path);
    error = "Could not save session manifest"; return false;
  }
  current = SessionInfo{};
  current.path = path; current.name = name;
  current.startedUtc = doc["started_utc"].as<String>(); current.clockSource = doc["clock_source"].as<String>();
  current.status = "active";
  startedMonotonic = monotonicMs(); running = true;
  Serial.println("[Specter] session started " + path);
  return true;
  #else
  error = "SD support required"; return false;
  #endif
}
String sessionDirectory(const char* category) {
  if (!running || !validOutputCategory(category)) return "";
  #ifdef HAS_SD
  if (!sd_obj.supported || !SD.exists(current.path + "/session.json")) {
    current.error = "Session SD storage unavailable"; return "";
  }
  const String path = current.path + "/" + category;
  if (!ensureDirectory(path)) { current.error = "Cannot create session output folder"; return ""; }
  return path;
  #else
  return "";
  #endif
}
bool registerOutput(const String& path, const char* kind) {
  if (!running) return true;
  #ifdef HAS_SD
  if (!sd_obj.supported || !path.startsWith("/") || path.indexOf("..") >= 0 || path.length() > 192 ||
      !kind || strlen(kind) > 24 || !SD.exists(path)) {
    current.error = "Cannot register session output"; return false;
  }
  StaticJsonDocument<512> doc;
  doc["path"] = path; doc["kind"] = kind;
  doc["utc"] = marauder::clockUtc(); doc["clock_source"] = marauder::clockTimeSource();
  doc["elapsed_ms"] = millisecondsText(monotonicMs() - startedMonotonic);
  char line[768];
  const size_t required = measureJson(doc);
  if (doc.overflowed() || required + 1 >= sizeof(line)) {
    current.error = "Session output metadata too large"; return false;
  }
  const size_t size = serializeJson(doc, line, sizeof(line));
  line[size] = '\n';
  const bool okay = fileio::appendSynced((String("/sd") + current.path + "/outputs.jsonl").c_str(), line, size + 1);
  if (!okay) { current.error = "Could not save session output index"; return false; }
  ++current.files;
  return true;
  #else
  return false;
  #endif
}
bool endSession(String& error) {
  error = "";
  if (!running) { error = "No session is active"; return false; }
  if (!idleForSessionChange()) { error = "Stop capture/jobs before ending"; return false; }
  #ifdef HAS_SD
  if (!sd_obj.supported) { error = "SD unavailable; session stays active"; return false; }
  buffer_obj.save();
  StaticJsonDocument<768> doc;
  const uint64_t duration = monotonicMs() - startedMonotonic;
  doc["schema"] = "specter.session.end.v1";
  doc["ended_utc"] = marauder::clockUtc();
  doc["clock_source"] = marauder::clockTimeSource();
  doc["elapsed_ms"] = millisecondsText(duration);
  doc["outputs"] = current.files;
  doc["storage_error"] = current.error;
  if (!writeSnapshot(current.path + "/complete.json", doc)) {
    error = "Cannot save completion; retry"; return false;
  }
  current.elapsedMs = duration; current.status = current.error.length() ? "closed-with-errors" : "closed";
  running = false;
  Serial.println("[Specter] session ended " + current.path);
  return true;
  #else
  error = "SD support required"; return false;
  #endif
}
bool readSession(const String& path, SessionInfo& info) {
  info = SessionInfo{};
  if (!validSessionPath(path.c_str())) return false;
  #ifdef HAS_SD
  if (!sd_obj.supported) return false;
  StaticJsonDocument<1024> start;
  if (!readJsonFile(path + "/session.json", start) ||
      strcmp(start["schema"] | "", "specter.session.v1") != 0) return false;
  info.path = path;
  info.name = start["name"].as<String>();
  info.startedUtc = start["started_utc"].as<String>();
  info.clockSource = start["clock_source"].as<String>();
  if (running && path == current.path) { info = activeSession(); return true; }
  StaticJsonDocument<768> end;
  if (readJsonFile(path + "/complete.json", end) &&
      strcmp(end["schema"] | "", "specter.session.end.v1") == 0) {
    info.error = end["storage_error"].as<String>();
    info.status = info.error.length() ? "closed-with-errors" : "closed";
    info.files = end["outputs"] | 0U;
    if (!parseElapsedMs(end["elapsed_ms"] | "", info.elapsedMs)) return false;
  } else { info.status = "interrupted"; info.files = UINT32_MAX; }
  return true;
  #else
  return false;
  #endif
}
size_t sessionPage(size_t offset, SessionInfo* rows, size_t capacity, bool& more) {
  more = false;
  if (!rows || !capacity) return 0;
  #ifdef HAS_SD
  if (!sd_obj.supported) return 0;
  File root = SD.open("/specter/sessions", FILE_READ);
  if (!root || !root.isDirectory()) { if (root) root.close(); return 0; }
  size_t skipped = 0, count = 0, visits = 0;
  File entry = root.openNextFile();
  while (entry) {
    const bool directory = entry.isDirectory();
    const String path = entry.path();
    entry.close();
    SessionInfo info;
    if (directory && readSession(path, info)) {
      if (skipped < offset) ++skipped;
      else if (count < capacity) rows[count++] = info;
      else { more = true; break; }
    }
    if (++visits % 16 == 0) yield();
    entry = root.openNextFile();
  }
  root.close();
  return count;
  #else
  (void)offset;
  return 0;
  #endif
}
size_t outputPage(const String& sessionPath, size_t offset, SessionOutput* rows, size_t capacity, bool& more) {
  more = false;
  if (!rows || !capacity || !validSessionPath(sessionPath.c_str())) return 0;
  #ifdef HAS_SD
  if (!sd_obj.supported) return 0;
  FILE* index = fopen((String("/sd") + sessionPath + "/outputs.jsonl").c_str(), "r");
  if (!index) return 0;
  char line[768];
  size_t skipped = 0, count = 0, visits = 0;
  while (fgets(line, sizeof(line), index)) {
    if (!strchr(line, '\n') && !feof(index)) {
      int c; while ((c = fgetc(index)) != EOF && c != '\n') {}
      continue;
    }
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, line)) continue;
    const String path = doc["path"].as<String>();
    if (!path.startsWith("/") || path.indexOf("..") >= 0 || path.length() > 192) continue;
    if (skipped < offset) ++skipped;
    else if (count < capacity) rows[count++] = {path, doc["kind"].as<String>(), doc["utc"].as<String>()};
    else { more = true; break; }
    if (++visits % 16 == 0) yield();
  }
  fclose(index);
  return count;
  #else
  (void)offset;
  return 0;
  #endif
}
bool handleSessionCommand(const String& input) {
  if (input == "session" || input == "session status") {
    const SessionInfo info = activeSession();
    Serial.printf("[Specter] %s name=%s files=%lu path=%s\n", running ? "active" : "idle",
      info.name.c_str(), (unsigned long)info.files, info.path.c_str());
    return true;
  }
  String error;
  if (input.startsWith("session start ")) {
    if (!startSession(input.substring(14), error)) Serial.println("[Specter] ERROR " + error);
    return true;
  }
  if (input == "session stop") {
    if (!endSession(error)) Serial.println("[Specter] ERROR " + error);
    return true;
  }
  if (input.startsWith("session ")) {
    Serial.println(F("Usage: session status | start <name> | stop")); return true;
  }
  return false;
}
}
