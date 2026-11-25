#include "AutomationTouch.h"

#if defined(HAS_SCREEN) && defined(HAS_TOUCH) && defined(HAS_ILI9341)
#include "MenuFunctions.h"
#include "TouchKeyboard.h"
#include "WifiAutomation.h"
#include "DeviceClock.h"
#include "UtcTime.h"
#include "IPv4Range.h"
#include "EvilPortal.h"
#include "hale_team.h"
#include <WiFi.h>

extern MenuFunctions menu_function_obj;
extern WiFiScan wifi_scan_obj;
extern Display display_obj;
extern EvilPortal evil_portal_obj;
#ifdef HAS_SD
extern SDInterface sd_obj;
#endif

namespace marauder {
namespace {
enum class View : uint8_t { NONE, PROBE, IOT, CLOCK, FILES, FILE_DATE };
enum class Picker : uint8_t { DATE, TEMPLATE };
View view = View::NONE;
Picker picker = Picker::DATE;
Menu* returnMenu = nullptr;
String targetSsid, firstIp, lastIp, selectedFile, chosenDate, uiNotice;
String browsePath = "/";
int browseOffset = 0;
LinkedList<SDDirectoryEntry> browseEntries;
struct Button { int16_t x, y, w, h; int action; };
Button buttons[10];
uint8_t buttonCount = 0;
bool ignoreUntilRelease = true, fingerDown = false;
int downAction = -1;
uint16_t lastX = 0, lastY = 0;
uint32_t lastDraw = 0;
uint8_t lastScanMode = WIFI_SCAN_OFF;
bool lastWaiting = false, lastPortal = false;
constexpr int FILES_PER_PAGE = 5;
constexpr int ACT_BACK = 1, ACT_EDIT_FIRST = 2, ACT_EDIT_LAST = 3,
              ACT_START = 4, ACT_STOP = 5, ACT_FILES = 6, ACT_EDIT_DATE = 7,
              ACT_APPLY_DATE = 8, ACT_REFRESH = 9, ACT_PREV = 10,
              ACT_NEXT = 11, ACT_UP = 12, ACT_ROW = 100;
int buttonBase() { return display_obj.tft.height() - 176; }
void textAt(int y, const String& text, uint16_t color = TFT_WHITE) {
  display_obj.tft.setFreeFont(nullptr);
  display_obj.tft.setTextSize(1);
  display_obj.tft.setTextFont(1);
  display_obj.tft.setTextColor(color, TFT_BLACK);
  display_obj.tft.setTextWrap(false);
  display_obj.tft.setCursor(8, y);
  const int chars = (display_obj.tft.width() - 16) / 6;
  display_obj.tft.print(text.substring(0, chars));
}
void twoLines(int y, const String& text, uint16_t color = TFT_LIGHTGREY) {
  const int chars = (display_obj.tft.width() - 16) / 6;
  textAt(y, text, color);
  if (static_cast<int>(text.length()) > chars) textAt(y + 10, text.substring(chars), color);
}
void button(int x, int y, int w, const String& label, int action, uint16_t color = TFT_WHITE) {
  if (buttonCount >= sizeof(buttons) / sizeof(buttons[0])) return;
  buttons[buttonCount++] = {int16_t(x), int16_t(y), int16_t(w), 30, action};
  display_obj.tft.drawRoundRect(x, y, w, 30, 4, color);
  display_obj.tft.setFreeFont(nullptr);
  display_obj.tft.setTextSize(1);
  display_obj.tft.setTextColor(color, TFT_BLACK);
  const int chars = (w - 8) / 8;
  display_obj.tft.drawCentreString(label.substring(0, chars), x + w / 2, y + 6, 2);
}
int hit(uint16_t x, uint16_t y) {
  for (uint8_t i = 0; i < buttonCount; ++i) {
    const Button& b = buttons[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return b.action;
  }
  return -1;
}
void releaseBeforeAction() {
  ignoreUntilRelease = true; fingerDown = false; downAction = -1;
}
String editableNow() {
  String text = clockValid() ? clockUtc().substring(0, 19) : String("");
  text.replace('T', ' ');
  return text;
}
bool inputText(String& value, size_t maxBytes, const char* title) {
  char buffer[128] = {};
  const size_t size = maxBytes + 1 < sizeof(buffer) ? maxBytes + 1 : sizeof(buffer);
  value.toCharArray(buffer, size);
  const bool accepted = keyboardInput(buffer, size, title);
  releaseBeforeAction();
  if (accepted) value = String(buffer);
  return accepted;
}
void seedRange() {
  if (WiFi.status() != WL_CONNECTED || firstIp.length()) return;
  const IPAddress self = WiFi.localIP(), mask = WiFi.subnetMask();
  const auto number = [](const IPAddress& ip) {
    return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
  };
  const IPv4HostRange range = ipv4HostRange(number(self), number(mask));
  if (!range.valid) return;
  uint32_t first = range.first;
  // On a broad subnet, propose the local /24-sized window rather than its far edge.
  const uint32_t localFirst = (number(self) & 0xFFFFFF00UL) + 1;
  if (localFirst >= range.first && localFirst <= range.last && localFirst <= 0xFFFFFF00UL) first = localFirst;
  const uint64_t capped = uint64_t(first) + 253;
  const uint32_t last = uint64_t(range.last) < capped ? range.last : uint32_t(capped);
  firstIp = IPAddress(first >> 24, first >> 16, first >> 8, first).toString();
  lastIp = IPAddress(last >> 24, last >> 16, last >> 8, last).toString();
}
void readDirectory(const String& path) {
  #ifdef HAS_SD
  browseEntries.clear();
  if (!sd_obj.supported || !sd_obj.listDirectory(path, &browseEntries)) {
    uiNotice = "Cannot read SD directory";
    return;
  }
  if (picker == Picker::TEMPLATE) {
    for (int i = browseEntries.size() - 1; i >= 0; --i) {
      const SDDirectoryEntry entry = browseEntries.get(i);
      if (!entry.is_directory && !entry.name.endsWith(".html")) browseEntries.remove(i);
    }
  }
  browsePath = path; browseOffset = 0;
  #else
  (void)path;
  uiNotice = "SD not supported";
  #endif
}
String fileModified() {
  #ifdef HAS_SD
  if (!sd_obj.supported) return "SD unavailable";
  File file = SD.open(selectedFile, FILE_READ);
  if (!file) return "File unavailable";
  const time_t modified = file.getLastWrite();
  file.close();
  return formatClockUtc(modified);
  #else
  return "SD not supported";
  #endif
}
void draw(bool full = true) {
  const int width = display_obj.tft.width(), height = display_obj.tft.height();
  if (full) {
    display_obj.clearScreen();
    menu_function_obj.drawStatusBar();
    buttonCount = 0;
  } else display_obj.tft.fillRect(0, 42, width, buttonBase() - 46, TFT_BLACK);
  display_obj.tft.setTextWrap(false);
  display_obj.tft.setFreeFont(nullptr);
  display_obj.tft.setTextSize(1);
  const int base = buttonBase(), wide = width - 16, half = (width - 24) / 2;
  const WifiAutomationStatus state = wifiAutomationStatus();
  if (view == View::FILES) {
    if (!full) return;
    display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
    display_obj.tft.drawCentreString(picker == Picker::DATE ? "Select SD file" : "Portal template", width / 2, 22, 2);
    textAt(43, browsePath, TFT_CYAN);
    for (int row = 0; row < FILES_PER_PAGE; ++row) {
      const int index = browseOffset + row;
      if (index >= browseEntries.size()) break;
      const SDDirectoryEntry entry = browseEntries.get(index);
      button(8, 60 + row * 32, wide, (entry.is_directory ? "[DIR] " : "") + entry.name,
             ACT_ROW + row, entry.is_directory ? TFT_YELLOW : TFT_WHITE);
    }
    button(8, height - 80, half, "< Prev", ACT_PREV);
    button(16 + half, height - 80, half, "Next >", ACT_NEXT);
    button(8, height - 44, half, "Up", ACT_UP);
    button(16 + half, height - 44, half, "Cancel", ACT_BACK);
    if (browseEntries.size() == 0) textAt(80, "No files / directory unavailable", TFT_ORANGE);
    return;
  }
  if (view == View::PROBE) {
    if (full) {
      display_obj.tft.setTextColor(TFT_RED, TFT_BLACK);
      display_obj.tft.drawCentreString("Probe to Portal", width / 2, 22, 2);
      button(8, base, wide, "Set target SSID", ACT_EDIT_FIRST);
      button(8, base + 34, wide, "Choose portal template", ACT_FILES);
      button(8, base + 68, half, "Start", ACT_START, TFT_RED);
      button(16 + half, base + 68, half, "Stop", ACT_STOP, TFT_ORANGE);
      button(8, base + 102, wide, "Back (stop job)", ACT_BACK);
    }
    textAt(46, String("State: ") + (state.probeWaiting ? "WAITING (60s)" : state.probePortal ? "PORTAL ACTIVE" : "IDLE"), TFT_WHITE);
    textAt(61, "SSID: " + (targetSsid.length() ? targetSsid : "<set target>"));
    textAt(76, "Page: " + evil_portal_obj.target_html_name);
    #if HALE_TEAM_MODE
    textAt(91, hale::offensiveAllowed() ? "Red Team: ready" : "Blue Team: portal locked", TFT_ORANGE);
    #endif
    twoLines(110, uiNotice.length() ? uiNotice : state.notice);
  } else if (view == View::IOT) {
    if (full) {
      display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
      display_obj.tft.drawCentreString("IoT Recon", width / 2, 22, 2);
      button(8, base, wide, "Edit first IP", ACT_EDIT_FIRST);
      button(8, base + 34, wide, "Edit last IP", ACT_EDIT_LAST);
      button(8, base + 68, half, "Start", ACT_START, TFT_RED);
      button(16 + half, base + 68, half, "Stop", ACT_STOP, TFT_ORANGE);
      button(8, base + 102, wide, "Back (stop job)", ACT_BACK);
    }
    textAt(46, WiFi.status() == WL_CONNECTED ? "LAN: " + WiFi.localIP().toString() : "Join a WiFi network first", TFT_ORANGE);
    textAt(59, firstIp + " - " + lastIp);
    textAt(72, state.iotRunning ? "Scanning: " + state.currentIp.toString() : "State: " + state.iotResult);
    textAt(85, "Checks: " + String(state.checks) + "   Open: " + String(state.openPorts));
    textAt(98, "Report: " + state.reportPath);
    twoLines(113, uiNotice.length() ? uiNotice : state.notice);
  } else if (view == View::CLOCK) {
    if (full) {
      display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
      display_obj.tft.drawCentreString("Clock / File Dates", width / 2, 22, 2);
      button(8, base, wide, "Set UTC date / time", ACT_EDIT_DATE);
      button(8, base + 34, wide, "Browse SD file dates", ACT_FILES);
      button(8, base + 68, wide, "Test SD dates", ACT_REFRESH);
      button(8, base + 102, wide, "Back", ACT_BACK);
    }
    textAt(47, "UTC: " + clockUtc(), clockValid() ? TFT_WHITE : TFT_ORANGE);
    textAt(65, "Source: " + clockTimeSource());
    textAt(83, "GPS sync is automatic after a fix.");
    twoLines(101, uiNotice.length() ? uiNotice : "Set UTC before creating new files.");
  } else if (view == View::FILE_DATE) {
    if (full) {
      display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
      display_obj.tft.drawCentreString("SD File Date (UTC)", width / 2, 22, 2);
      button(8, base, wide, "Choose known date", ACT_EDIT_DATE);
      button(8, base + 34, wide, "Apply date to file", ACT_APPLY_DATE, TFT_RED);
      button(8, base + 68, wide, "Choose another file", ACT_FILES);
      button(8, base + 102, wide, "Back to clock", ACT_BACK);
    }
    twoLines(46, selectedFile, TFT_CYAN);
    textAt(71, "Old: " + fileModified());
    textAt(86, "New: " + (chosenDate.length() ? chosenDate : "<choose known UTC date>"));
    twoLines(105, uiNotice.length() ? uiNotice : "Only metadata changes; file kept.");
  }
  const char* footer = view == View::PROBE ? "Exact SSID match; 60 second wait." :
    view == View::IOT ? "TCP services; report saved on SD." :
    view == View::FILE_DATE ? "UTC date; file contents unchanged." :
    "Unsynced clock = unknown file dates.";
  textAt(height - 25, footer, TFT_LIGHTGREY);
  lastDraw = millis();
  lastScanMode = wifi_scan_obj.currentScanMode;
  lastWaiting = state.probeWaiting; lastPortal = state.probePortal;
}
void closePanel() {
  if (view == View::PROBE) handleWifiAutomationCommand("probetwin stop");
  if (view == View::IOT) handleWifiAutomationCommand("iotrecon stop");
  view = View::NONE;
  browseEntries.clear();
  display_obj.exit_draw = false;
  wifi_scan_obj.orient_display = false;
  if (returnMenu) menu_function_obj.changeMenu(returnMenu);
  releaseBeforeAction();
}
void startPicker(Picker kind) {
  #ifdef HAS_SD
  if (!sd_obj.supported) { uiNotice = "SD unavailable"; return; }
  picker = kind; readDirectory("/"); view = View::FILES;
  #else
  (void)kind; uiNotice = "SD not supported";
  #endif
}
void dispatch(int action) {
  const WifiAutomationStatus state = wifiAutomationStatus();
  if (view == View::FILES) {
    if (action == ACT_BACK) view = picker == Picker::DATE ? View::CLOCK : View::PROBE;
    else if (action == ACT_UP) {
      const int slash = browsePath.lastIndexOf('/');
      readDirectory(slash <= 0 ? String("/") : browsePath.substring(0, slash));
    } else if (action == ACT_PREV) browseOffset = browseOffset >= FILES_PER_PAGE ? browseOffset - FILES_PER_PAGE : 0;
    else if (action == ACT_NEXT && browseOffset + FILES_PER_PAGE < browseEntries.size()) browseOffset += FILES_PER_PAGE;
    else if (action >= ACT_ROW && action < ACT_ROW + FILES_PER_PAGE) {
      const int index = browseOffset + action - ACT_ROW;
      if (index >= browseEntries.size()) return;
      const SDDirectoryEntry entry = browseEntries.get(index);
      if (entry.is_directory) readDirectory(entry.path);
      else if (picker == Picker::TEMPLATE) {
        if (!entry.name.endsWith(".html")) { uiNotice = "Choose an .html portal template"; return; }
        evil_portal_obj.target_html_name = entry.path.startsWith("/") ? entry.path.substring(1) : entry.path;
        evil_portal_obj.using_serial_html = false;
        uiNotice = "Template selected"; view = View::PROBE;
      } else {
        selectedFile = entry.path; chosenDate = ""; uiNotice = "Choose date, review, then Apply";
        view = View::FILE_DATE;
      }
    }
  } else if (action == ACT_BACK) {
    if (view == View::FILE_DATE) { view = View::CLOCK; uiNotice = ""; }
    else closePanel();
  } else if (action == ACT_STOP) {
    handleWifiAutomationCommand(view == View::PROBE ? "probetwin stop" : "iotrecon stop"); uiNotice = "Stopped";
  } else if (action == ACT_START) {
    uiNotice = "";
    if (view == View::PROBE) {
      if (!targetSsid.length()) uiNotice = "Set a target SSID first";
      else {
        #if HALE_TEAM_MODE
        if (hale::offensiveAllowed() && wifi_scan_obj.currentScanMode == WIFI_CONNECTED)
          wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        #else
        if (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        #endif
        handleWifiAutomationCommand("probetwin start " + targetSsid);
      }
    } else if (view == View::IOT) handleWifiAutomationCommand("iotrecon start " + firstIp + " " + lastIp);
  } else if (action == ACT_EDIT_FIRST || action == ACT_EDIT_LAST || action == ACT_FILES || action == ACT_EDIT_DATE) {
    if (state.probeWaiting || state.probePortal || state.iotRunning) {
      uiNotice = "Stop the current job before editing";
    } else if (action == ACT_EDIT_FIRST) {
      inputText(view == View::PROBE ? targetSsid : firstIp, view == View::PROBE ? 31 : 15,
                view == View::PROBE ? "Exact target SSID" : "First IPv4 address");
      uiNotice = "";
    } else if (action == ACT_EDIT_LAST) { inputText(lastIp, 15, "Last IPv4 address"); uiNotice = ""; }
    else if (action == ACT_FILES) startPicker(view == View::PROBE ? Picker::TEMPLATE : Picker::DATE);
    else {
      String date = view == View::FILE_DATE && chosenDate.length() ? chosenDate : editableNow();
      if (inputText(date, 20, "UTC: YYYY-MM-DD HH:MM:SS")) {
        uint64_t epoch = 0;
        if (!parseUtcDateTime(date.c_str(), epoch)) uiNotice = "Invalid UTC date / time";
        else if (view == View::FILE_DATE) { chosenDate = date; uiNotice = "Review date; tap Apply to save"; }
        else if (setClockUtc(epoch, "manual")) uiNotice = "UTC set for new file writes";
        else uiNotice = "Could not set the clock";
      }
    }
  } else if (action == ACT_APPLY_DATE && view == View::FILE_DATE) {
    uint64_t epoch = 0;
    if (state.probeWaiting || state.probePortal || state.iotRunning) uiNotice = "Stop the current job first";
    else if (!parseUtcDateTime(chosenDate.c_str(), epoch)) uiNotice = "Choose a valid known date first";
    else {
      String error;
      uiNotice = setFileDateUtc(selectedFile, epoch, error) ? "File date updated; contents kept" : error;
    }
  } else if (action == ACT_REFRESH) {
    if (state.probeWaiting || state.probePortal || state.iotRunning) uiNotice = "Stop the current job first";
    else testSdClock(uiNotice);
  }
  if (view != View::NONE) draw();
}
}

bool automationTouchActive() { return view != View::NONE; }
void showAutomationPanel(AutomationPanel panel) {
  if (wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF && wifi_scan_obj.currentScanMode != WIFI_CONNECTED) {
    Serial.println(F("Stop the current scan before opening automation controls")); return;
  }
  returnMenu = menu_function_obj.current_menu;
  view = panel == AutomationPanel::PROBE ? View::PROBE : panel == AutomationPanel::IOT ? View::IOT : View::CLOCK;
  uiNotice = "";
  seedRange();
  display_obj.tft.setRotation(SCREEN_ORIENTATION);
  display_obj.setCalData(false);
  display_obj.exit_draw = false;
  wifi_scan_obj.orient_display = false;
  releaseBeforeAction(); draw();
}
bool automationTouchLoop(uint32_t now) {
  if (view == View::NONE) return false;
  const WifiAutomationStatus state = wifiAutomationStatus();
  if (view != View::FILES && (uint32_t(now - lastDraw) >= 1000 ||
      lastScanMode != wifi_scan_obj.currentScanMode || state.probeWaiting != lastWaiting || state.probePortal != lastPortal))
    draw(lastScanMode != wifi_scan_obj.currentScanMode || state.probeWaiting != lastWaiting || state.probePortal != lastPortal);
  uint16_t x = 0, y = 0;
  const bool down = display_obj.updateTouch(&x, &y) != 0;
  if (ignoreUntilRelease) {
    if (!down) ignoreUntilRelease = false;
    return true;
  }
  if (down) {
    if (!fingerDown) downAction = hit(x, y);
    lastX = x; lastY = y; fingerDown = true;
  } else if (fingerDown) {
    const int action = downAction;
    fingerDown = false; downAction = -1;
    if (action >= 0 && action == hit(lastX, lastY)) {
      dispatch(action);
      releaseBeforeAction();
    }
  }
  return true;
}
}
#else
namespace marauder {
void showAutomationPanel(AutomationPanel) {}
bool automationTouchActive() { return false; }
bool automationTouchLoop(uint32_t) { return false; }
}
#endif
