#include "BadBleScript.h"
#include <algorithm>
#include <cctype>
#include <cstring>
namespace specter {
namespace {
constexpr uint8_t MOD_LCTRL=1, MOD_LSHIFT=2, MOD_LALT=4, MOD_LGUI=8;
struct BbKeyName { const char* name; uint8_t key; };
static const BbKeyName s_keyNames[] = {
    { "ENTER", 0x28 }, { "RETURN", 0x28 },
    { "ESC", 0x29 },   { "ESCAPE", 0x29 },
    { "BACKSPACE", 0x2A }, { "BS", 0x2A },
    { "TAB", 0x2B },   { "SPACE", 0x2C },
    { "CAPSLOCK", 0x39 }, { "DELETE", 0x4C }, { "DEL", 0x4C },
    { "INSERT", 0x49 }, { "HOME", 0x4A }, { "END", 0x4D },
    { "PAGEUP", 0x4B }, { "PAGEDOWN", 0x4E },
    { "UP", 0x52 },    { "UPARROW", 0x52 },
    { "DOWN", 0x51 },  { "DOWNARROW", 0x51 },
    { "LEFT", 0x50 },  { "LEFTARROW", 0x50 },
    { "RIGHT", 0x4F }, { "RIGHTARROW", 0x4F },
    { "PRINTSCREEN", 0x46 }, { "PRTSC", 0x46 },
    { "SCROLLLOCK", 0x47 },  { "PAUSE", 0x48 }, { "NUMLOCK", 0x53 },
    { "MENU", 0x65 },  { "APP", 0x65 },
    { "F1", 0x3A }, { "F2", 0x3B }, { "F3", 0x3C }, { "F4", 0x3D },
    { "F5", 0x3E }, { "F6", 0x3F }, { "F7", 0x40 }, { "F8", 0x41 },
    { "F9", 0x42 }, { "F10", 0x43 }, { "F11", 0x44 }, { "F12", 0x45 },
};

// US-layout character -> (usage id, needs-shift). Non-US physical layouts
// type some symbols differently — documented limitation, see the notes.
static bool char_to_key(char c, uint8_t& key, bool& shift) {
    shift = false;
    if (c >= 'a' && c <= 'z') { key = (uint8_t)(0x04 + (c - 'a')); return true; }
    if (c >= 'A' && c <= 'Z') { key = (uint8_t)(0x04 + (c - 'A')); shift = true; return true; }
    if (c >= '1' && c <= '9') { key = (uint8_t)(0x1E + (c - '1')); return true; }
    switch (c) {
        case '0': key = 0x27; return true;
        case ' ': key = 0x2C; return true;
        case '\n': key = 0x28; return true;               // Enter
        case '\t': key = 0x2B; return true;               // Tab
        case '!': key = 0x1E; shift = true; return true;
        case '@': key = 0x1F; shift = true; return true;
        case '#': key = 0x20; shift = true; return true;
        case '$': key = 0x21; shift = true; return true;
        case '%': key = 0x22; shift = true; return true;
        case '^': key = 0x23; shift = true; return true;
        case '&': key = 0x24; shift = true; return true;
        case '*': key = 0x25; shift = true; return true;
        case '(': key = 0x26; shift = true; return true;
        case ')': key = 0x27; shift = true; return true;
        case '-': key = 0x2D; return true;
        case '_': key = 0x2D; shift = true; return true;
        case '=': key = 0x2E; return true;
        case '+': key = 0x2E; shift = true; return true;
        case '[': key = 0x2F; return true;
        case '{': key = 0x2F; shift = true; return true;
        case ']': key = 0x30; return true;
        case '}': key = 0x30; shift = true; return true;
        case '\\': key = 0x31; return true;
        case '|': key = 0x31; shift = true; return true;
        case ';': key = 0x33; return true;
        case ':': key = 0x33; shift = true; return true;
        case '\'': key = 0x34; return true;
        case '"': key = 0x34; shift = true; return true;
        case '`': key = 0x35; return true;
        case '~': key = 0x35; shift = true; return true;
        case ',': key = 0x36; return true;
        case '<': key = 0x36; shift = true; return true;
        case '.': key = 0x37; return true;
        case '>': key = 0x37; shift = true; return true;
        case '/': key = 0x38; return true;
        case '?': key = 0x38; shift = true; return true;
        default:  return false;                            // incl. '\r'
    }
}


std::string upper(std::string text) {
  for (auto& c : text) c = std::toupper(static_cast<unsigned char>(c));
  return text;
}
bool number(const std::string& value, uint32_t maximum, uint32_t& out) {
  if (value.empty()) return false;
  out = 0;
  for (auto c : value) {
    if (c < '0' || c > '9') return false;
    const uint32_t digit = c - '0';
    if (out > maximum / 10 || (out == maximum / 10 && digit > maximum % 10)) return false;
    out = out * 10 + digit;
  }
  return true;
}
}
bool BadBleScript::compile(const std::string& body) {
  count = characters = errorLine = 0; error = "";
  size_t anchor = 0, anchorCount = 0, lineNumber = 0;
  uint32_t defaultDelay = 0;
  auto fail = [&](const char* reason) { error = reason; errorLine = lineNumber; count = characters = 0; return false; };
  auto add = [&](uint8_t mod, uint8_t key, bool character, uint32_t gap) {
    if (count >= steps.size()) return false;
    steps[count++] = {mod, key, gap, character};
    if (character) ++characters;
    return true;
  };
  if (body.size() > badBleMaxBytes) return fail("Script exceeds 32 KiB");
  if (body.find('\0') != std::string::npos) return fail("NUL byte in script");
  for (size_t pos = 0; pos < body.size();) {
    ++lineNumber;
    size_t end = body.find('\n', pos); if (end == std::string::npos) end = body.size();
    std::string line = body.substr(pos, end - pos); pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t begin = line.find_first_not_of(" \t");
    if (begin == std::string::npos) continue;
    line.erase(0, begin);
    const auto split = line.find_first_of(" \t");
    const auto op = upper(line.substr(0, split));
    if (op == "REM") continue;
    std::string rest = split == std::string::npos ? "" : line.substr(split + 1);
    const size_t before = count;
    if (op == "STRING" || op == "STRINGLN") {
      if (op == "STRINGLN") rest += '\n';
      for (char c : rest) {
        uint8_t key = 0; bool shift = false;
        if (!char_to_key(c, key, shift)) return fail("Unsupported character (US ASCII layout)");
        if (!add(shift ? MOD_LSHIFT : 0, key, true, 35 + defaultDelay)) return fail("Script exceeds 1024 steps");
      }
    } else if (op == "DELAY" || op == "DEFAULTDELAY" || op == "DEFAULT_DELAY" || op == "REPEAT") {
      const auto first = rest.find_first_not_of(" \t"), last = rest.find_last_not_of(" \t");
      rest = first == std::string::npos ? "" : rest.substr(first, last - first + 1);
      uint32_t n = 0;
      if (!number(rest, op == "DELAY" ? 60000 : op == "REPEAT" ? 1024 : 5000, n)) return fail("Invalid or excessive numeric argument");
      if (op == "DELAY") {
        if (!add(0, 0, false, n)) return fail("Script exceeds 1024 steps");
      } else if (op != "REPEAT") { defaultDelay = n; continue; }
      else {
        if (!anchorCount || !n) return fail("REPEAT needs a previous command and positive count");
        if (n > (steps.size() - count) / anchorCount) return fail("REPEAT exceeds 1024 steps");
        for (uint32_t rep = 0; rep < n; ++rep) {
          for (size_t j = 0; j < anchorCount; ++j) {
            const auto copy = steps[anchor + j];
            add(copy.mod, copy.key, copy.character, copy.afterMs);
          }
        }
        continue; // Consecutive REPEATs keep the original command anchor.
      }
    } else {
      std::string combo = upper(line);
      std::replace(combo.begin(), combo.end(), '-', ' ');
      uint8_t mod = 0, key = 0; bool haveKey = false;
      for (size_t i = 0; i < combo.size();) {
        i = combo.find_first_not_of(" \t", i); if (i == std::string::npos) break;
        size_t stop = combo.find_first_of(" \t", i); if (stop == std::string::npos) stop = combo.size();
        const auto part = combo.substr(i, stop - i); i = stop;
        if (part == "CTRL" || part == "CONTROL") mod |= MOD_LCTRL;
        else if (part == "SHIFT") mod |= MOD_LSHIFT;
        else if (part == "ALT") mod |= MOD_LALT;
        else if (part == "GUI" || part == "WINDOWS" || part == "CMD" || part == "META") mod |= MOD_LGUI;
        else {
          if (haveKey) return fail("Only one non-modifier key per combo is supported");
          for (const auto& item : s_keyNames) if (part == item.name) { key = item.key; haveKey = true; break; }
          if (!haveKey && part.size() == 1) {
            bool shift = false;
            haveKey = char_to_key(std::tolower(static_cast<unsigned char>(part[0])), key, shift);
            if (shift) mod |= MOD_LSHIFT;
          }
          if (!haveKey) return fail("Unknown command or key name");
        }
      }
      if (!mod && !haveKey) return fail("Empty key combination");
      if (!add(mod, key, false, 40 + defaultDelay)) return fail("Script exceeds 1024 steps");
    }
    if (count > before) { anchor = before; anchorCount = count - before; }
  }
  if (!count) return fail("Script has no executable commands");
  return true;
}
}
