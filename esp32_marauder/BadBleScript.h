#pragma once
#include <array>
#include <string>
#include <stdint.h>
#include <stddef.h>
namespace specter {
constexpr size_t badBleMaxSteps = 1024;
constexpr size_t badBleMaxBytes = 32768;
// Parser and key tables adapted from the user's supplied bt_badble.cpp.
struct BadBleStep {
  uint8_t mod = 0, key = 0;
  uint32_t afterMs = 0;
  bool character = false;
};
class BadBleScript {
public:
  std::array<BadBleStep, badBleMaxSteps> steps{};
  size_t count = 0, characters = 0, errorLine = 0;
  const char* error = "";
  bool compile(const std::string& body);
};
// Same press/release/gap phases as the supplied engine, isolated for host tests.
class BadBleRunner {
public:
  size_t step = 0, characters = 0;
  bool running = false, completed = false;
  const char* result = "Idle";
  void start(uint32_t now) {
    step = characters = 0; phase = 0; next = now + 1500;
    running = true; completed = false; result = "Starting in 1.5s";
  }
  template<class Send> void stop(Send send, const char* why) {
    if (running) { const uint8_t empty[8] = {}; send(empty); }
    running = false; completed = false; result = why;
  }
  template<class Send> void tick(const BadBleScript& script, uint32_t now, bool ready, Send send) {
    if (!running) return;
    if (!ready) { stop(send, "Cancelled: host unavailable"); return; }
    if (int32_t(now - next) < 0) return;
    if (step >= script.count) { running = false; completed = true; result = "Complete"; return; }
    const auto& st = script.steps[step];
    if (phase == 0) {
      result = "Typing";
      if (!st.mod && !st.key) { phase = 2; next = now + st.afterMs; return; }
      uint8_t report[8] = {st.mod, 0, st.key, 0, 0, 0, 0, 0};
      if (!send(report)) { stop(send, "Stopped: notification failed"); return; }
      phase = 1; next = now + 35; return;
    }
    if (phase == 1) {
      const uint8_t empty[8] = {};
      if (!send(empty)) { stop(send, "Stopped: release failed"); return; }
      if (st.character) ++characters;
      phase = 2; next = now + st.afterMs; return;
    }
    phase = 0; ++step; next = now;
  }
private:
  uint8_t phase = 0;
  uint32_t next = 0;
};
}
