#pragma once
#include <Arduino.h>
#include <stddef.h>

namespace specter {
struct SessionInfo {
  String path;
  String name;
  String startedUtc;
  String clockSource;
  String status;
  String error;
  uint32_t files = 0; // UINT32_MAX means unknown after interruption.
  uint64_t elapsedMs = 0;
};
void setExternalBusyHook(bool (*hook)()); // Future managers must report any external active jobs.
bool sessionActive();
SessionInfo activeSession();
bool startSession(const String& name, String& error);
bool endSession(String& error);
// Empty means unavailable, or no active session; callers must not silently fall back
// to another folder if sessionActive() is true and this fails.
String sessionDirectory(const char* category);
bool registerOutput(const String& path, const char* kind);
bool readSession(const String& path, SessionInfo& info);
size_t sessionPage(size_t offset, SessionInfo* rows, size_t capacity, bool& more);
struct SessionOutput { String path; String kind; String utc; };
size_t outputPage(const String& sessionPath, size_t offset, SessionOutput* rows, size_t capacity, bool& more);
bool handleSessionCommand(const String& input);
}
