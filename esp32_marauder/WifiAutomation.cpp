// Original MarauderHale workflow orchestration; reuses existing Marauder radios/portal.
#include "WifiAutomation.h"
#include "DeviceClock.h"
#include "SpecterSessions.h"
#include "ProbeMatch.h"
#include "AutomationRange.h"
#include "hale_team.h"
#include "WiFiScan.h"
#include "MenuFunctions.h"
#include "EvilPortal.h"
#include <WiFi.h>
#include <stdio.h>
#include <unistd.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

extern WiFiScan wifi_scan_obj;
extern EvilPortal evil_portal_obj;
#ifdef HAS_SCREEN
extern MenuFunctions menu_function_obj;
extern Display display_obj;
#endif
#ifdef HAS_SD
extern SDInterface sd_obj;
#endif

namespace marauder {
namespace {
portMUX_TYPE probeMux = portMUX_INITIALIZER_UNLOCKED;
bool probeArmed = false;
bool probeSeen = false;
bool probePortalActive = false;
char probeSsid[33] = {};
size_t probeLength = 0;
uint32_t probeStarted = 0;
bool iotRunning = false;
String lastAutomationNotice;
String iotResult = "idle";
String iotReportPath;
void announce(const String& text) {
  lastAutomationNotice = text;
  Serial.println(text);
}
uint32_t iotCurrent = 0, iotLast = 0, iotSelf = 0, iotMask = 0;
uint8_t iotBssid[6] = {};
uint32_t iotChecks = 0, iotOpen = 0, iotLastTick = 0;
uint8_t iotPortIndex = 0;
struct Service { uint16_t port; const char* label; };
const Service services[] = {{21,"ftp"},{22,"ssh"},{23,"telnet"},{80,"http"},
  {443,"https"},{445,"smb"},{1883,"mqtt"},{8080,"http-alt"},{8883,"mqtt-tls"}};
constexpr size_t serviceCount = sizeof(services)/sizeof(services[0]);
#ifdef HAS_SD
FILE* iotFile = nullptr;
#endif
uint32_t ipNumber(const IPAddress& ip) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) |
         (uint32_t(ip[2]) << 8) | ip[3];
}
IPAddress numberIp(uint32_t ip) {
  return IPAddress(ip >> 24, ip >> 16, ip >> 8, ip);
}
void finishIot(const char* reason) {
  if (!iotRunning) return;
  #ifdef HAS_SD
  if (iotFile) {
    bool saved = fprintf(iotFile, "# end=%s,utc=%s,checks=%lu,open=%lu\n", reason,
      clockUtc().c_str(), (unsigned long)iotChecks, (unsigned long)iotOpen) > 0;
    if (fflush(iotFile) != 0 || fsync(fileno(iotFile)) != 0) saved = false;
    if (fclose(iotFile) != 0) saved = false;
    iotFile = nullptr;
    if (!saved) reason = "error-SD-finalize";
  }
  #endif
  iotRunning = false;
  iotResult = reason;
  lastAutomationNotice = String("[iotrecon] ") + reason;
  Serial.printf("[iotrecon] %s checks=%lu open=%lu\n", reason,
                (unsigned long)iotChecks, (unsigned long)iotOpen);
}
void showScanScreen() {
  #ifdef HAS_SCREEN
  display_obj.clearScreen();
  menu_function_obj.drawStatusBar();
  #endif
}
}

bool observeAutomationProbe(const uint8_t* frame, size_t length) {
  // Called on the WiFi task: fixed storage only, no portal/radio/file operations.
  portENTER_CRITICAL(&probeMux);
  const bool consumed = probeArmed;
  if (probeArmed && !probeSeen && probeMatches(frame, length, probeSsid, probeLength))
    probeSeen = true;
  portEXIT_CRITICAL(&probeMux);
  return consumed;
}
WifiAutomationStatus wifiAutomationStatus() {
  char ssid[33];
  portENTER_CRITICAL(&probeMux);
  const bool waiting = probeArmed;
  memcpy(ssid, probeSsid, sizeof(ssid));
  portEXIT_CRITICAL(&probeMux);
  return {waiting, probePortalActive, iotRunning, String(ssid), lastAutomationNotice,
          iotResult, iotReportPath, numberIp(iotCurrent), iotChecks, iotOpen};
}
void stopWifiAutomation() {
  portENTER_CRITICAL(&probeMux);
  probeArmed = false; probeSeen = false;
  portEXIT_CRITICAL(&probeMux);
  probePortalActive = false;
  finishIot("stopped");
}

bool handleWifiAutomationCommand(const String& input) {
  #if !HALE_WIFI_PROBE_TWIN
  if (input == "probetwin" || input.startsWith("probetwin ")) {
    announce(F("[probetwin] ERROR feature disabled in this build")); return true;
  }
  #endif
  #if !HALE_IOT_RECON
  if (input == "iotrecon" || input.startsWith("iotrecon ")) {
    announce(F("[iotrecon] ERROR feature disabled in this build")); return true;
  }
  #endif
  if (input == "probetwin status") {
    portENTER_CRITICAL(&probeMux);
    const bool armed = probeArmed;
    portEXIT_CRITICAL(&probeMux);
    Serial.printf("[probetwin] %s\n", armed ? "waiting" :
      (probePortalActive && wifi_scan_obj.currentScanMode == WIFI_SCAN_EVIL_PORTAL ? "portal-active" : "idle"));
    return true;
  }
  if (input == "probetwin stop") {
    portENTER_CRITICAL(&probeMux);
    const bool armed = probeArmed;
    probeArmed = false; probeSeen = false;
    portEXIT_CRITICAL(&probeMux);
    if (armed && wifi_scan_obj.currentScanMode == WIFI_SCAN_PROBE)
      wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
    else if (probePortalActive && wifi_scan_obj.currentScanMode == WIFI_SCAN_EVIL_PORTAL)
      wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
    probePortalActive = false;
    announce(F("[probetwin] stopped"));
    return true;
  }
  if (input.startsWith("probetwin start ")) {
    #if HALE_TEAM_MODE
    if (!hale::offensiveAllowed()) {
      announce(F("[probetwin] ERROR Blue Team: enable Red Team first")); return true;
    }
    #endif
    const String target = input.substring(16);
    if (!target.length() || target.length() > 31 || target.indexOf('\n') >= 0) {
      announce(F("[probetwin] ERROR SSID must be 1..31 bytes")); return true;
    }
    if (wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF || iotRunning) {
      announce(F("[probetwin] ERROR stop the current operation first")); return true;
    }
    // Portal implementation supports 31 bytes plus terminator. Template must be configured.
    #ifdef HAS_SD
    if (!sd_obj.supported || !SD.exists("/" + evil_portal_obj.target_html_name)) {
      announce(F("[probetwin] ERROR configure an existing SD HTML template first"));
      return true;
    }
    #else
    announce(F("[probetwin] ERROR SD template required")); return true;
    #endif
    showScanScreen();
    wifi_scan_obj.StartScan(WIFI_SCAN_PROBE, TFT_CYAN);
    if (wifi_scan_obj.currentScanMode != WIFI_SCAN_PROBE) {
      announce(F("[probetwin] ERROR probe scan did not start")); return true;
    }
    portENTER_CRITICAL(&probeMux);
    memcpy(probeSsid, target.c_str(), target.length() + 1);
    probeLength = target.length(); probeSeen = false; probeArmed = true;
    portEXIT_CRITICAL(&probeMux);
    probeStarted = millis();
    announce("[probetwin] waiting ssid=" + target + " timeout=60s");
    return true;
  }
  if (input == "probetwin" || input.startsWith("probetwin ")) {
    announce(F("Usage: probetwin start <exact SSID> | status | stop")); return true;
  }
  if (input == "iotrecon stop") { finishIot("stopped"); return true; }
  if (input == "iotrecon status") {
    Serial.printf("[iotrecon] %s checks=%lu open=%lu\n", iotRunning ? "running" : "idle",
      (unsigned long)iotChecks, (unsigned long)iotOpen); return true;
  }
  if (input.startsWith("iotrecon start ")) {
    if (iotRunning || WiFi.status() != WL_CONNECTED || wifi_scan_obj.currentScanMode != WIFI_CONNECTED) {
      announce(F("[iotrecon] ERROR join a network and stop other operations first")); return true;
    }
    const int split = input.indexOf(' ', 15);
    IPAddress first, last;
    if (split < 0 || !first.fromString(input.substring(15, split)) ||
        !last.fromString(input.substring(split + 1))) {
      announce(F("Usage: iotrecon start <first IPv4> <last IPv4>")); return true;
    }
    const uint32_t lo = ipNumber(first), hi = ipNumber(last);
    const uint32_t mask = ipNumber(WiFi.subnetMask());
    const uint32_t self = ipNumber(WiFi.localIP());
    if (!validAutomationRange(lo, hi, self, mask)) {
      announce(F("[iotrecon] ERROR use 1..254 host addresses on the connected subnet")); return true;
    }
    #ifdef HAS_SD
    if (!sd_obj.supported) { announce(F("[iotrecon] ERROR SD unavailable")); return true; }
    const String reportDirectory = specter::sessionActive() ? specter::sessionDirectory("reports") : String("/iot_recon");
    if (!reportDirectory.length() || (!SD.exists(reportDirectory) && !SD.mkdir(reportDirectory))) {
      announce(F("[iotrecon] ERROR cannot create report directory")); return true;
    }
    String path;
    bool available = false;
    for (unsigned i = 1; i <= 9999; ++i) {
      path = reportDirectory + (specter::sessionActive() ? "/iot_" : "/session_") + String(i) + ".csv";
      if (!SD.exists(path)) { available = true; break; }
    }
    if (!available) { announce(F("[iotrecon] ERROR report names exhausted")); return true; }
    iotFile = fopen((String("/sd") + path).c_str(), "w");
    if (!iotFile) { announce(F("[iotrecon] ERROR cannot open report")); return true; }
    // stdio exposes write/flush/fsync failures which Arduino File::flush hides.
    const bool headerOk = fprintf(iotFile, "utc,uptime_ms,ip,port,service,result\n") > 0 &&
      fprintf(iotFile, "# start=%s,end=%s,clock=%s\n", first.toString().c_str(), last.toString().c_str(), clockUtc().c_str()) > 0;
    if (!headerOk || fflush(iotFile) != 0 || fsync(fileno(iotFile)) != 0) {
      fclose(iotFile); iotFile = nullptr;
      announce(F("[iotrecon] ERROR SD write failed")); return true;
    }
    if (!specter::registerOutput(path, "iot-report")) {
      fclose(iotFile); iotFile = nullptr;
      announce(F("[iotrecon] ERROR cannot index session report")); return true;
    }
    iotReportPath = path;
    announce("[iotrecon] report=" + path);
    #else
    announce(F("[iotrecon] ERROR SD required for reports")); return true;
    #endif
    iotCurrent = lo; iotLast = hi; iotSelf = self; iotMask = mask; iotPortIndex = 0;
    memcpy(iotBssid, WiFi.BSSID(), sizeof(iotBssid));
    iotChecks = iotOpen = iotLastTick = 0; iotRunning = true; iotResult = "running";
    announce(F("[iotrecon] started; service labels are port conventions, not device identification"));
    if (!clockValid()) announce(F("[iotrecon] clock unsynced; report includes uptime; use clock set or GPS"));
    return true;
  }
  if (input == "iotrecon" || input.startsWith("iotrecon ")) {
    announce(F("Usage: iotrecon start <first IPv4> <last IPv4> | status | stop")); return true;
  }
  return false;
}

void wifiAutomationTick(uint32_t now) {
  #if HALE_TEAM_MODE
  if (!hale::offensiveAllowed()) {
    portENTER_CRITICAL(&probeMux);
    const bool wasArmed = probeArmed;
    probeArmed = false; probeSeen = false;
    portEXIT_CRITICAL(&probeMux);
    if ((wasArmed && wifi_scan_obj.currentScanMode == WIFI_SCAN_PROBE) ||
        (probePortalActive && wifi_scan_obj.currentScanMode == WIFI_SCAN_EVIL_PORTAL))
      wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
    if (wasArmed || probePortalActive) announce(F("[probetwin] cancelled: Blue Team"));
    probePortalActive = false;
  }
  #endif
  if (probePortalActive && wifi_scan_obj.currentScanMode != WIFI_SCAN_EVIL_PORTAL)
    probePortalActive = false;
  bool armed, seen;
  char target[33];
  portENTER_CRITICAL(&probeMux);
  armed = probeArmed; seen = probeSeen;
  memcpy(target, probeSsid, sizeof(target));
  portEXIT_CRITICAL(&probeMux);
  if (armed) {
    if (wifi_scan_obj.currentScanMode != WIFI_SCAN_PROBE) {
      portENTER_CRITICAL(&probeMux);
      probeArmed = false; probeSeen = false;
      portEXIT_CRITICAL(&probeMux);
      announce(F("[probetwin] cancelled: scan changed"));
    } else if (seen || uint32_t(now - probeStarted) >= 60000) {
      stopWifiAutomation();
      wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
      if (!seen) { announce(F("[probetwin] timeout")); return; }
      if (!evil_portal_obj.setAP(String(target))) {
        announce(F("[probetwin] ERROR cannot set portal SSID")); return;
      }
      evil_portal_obj.ap_index = -1;
      showScanScreen();
      wifi_scan_obj.StartScan(WIFI_SCAN_EVIL_PORTAL, TFT_ORANGE);
      wifi_scan_obj.setMac();
      probePortalActive = wifi_scan_obj.currentScanMode == WIFI_SCAN_EVIL_PORTAL &&
                          evil_portal_obj.active() && WiFi.softAPSSID() == String(target) &&
                          WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
      if (!probePortalActive) {
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        announce(F("[probetwin] ERROR portal failed to start")); return;
      }
      announce("[probetwin] portal active ssid=" + String(target));
    }
  }
  if (!iotRunning || uint32_t(now - iotLastTick) < 100) return;
  iotLastTick = now;
  if (WiFi.status() != WL_CONNECTED || wifi_scan_obj.currentScanMode != WIFI_CONNECTED ||
      ipNumber(WiFi.localIP()) != iotSelf || ipNumber(WiFi.subnetMask()) != iotMask ||
      !WiFi.BSSID() || memcmp(WiFi.BSSID(), iotBssid, sizeof(iotBssid)) != 0) { finishIot("cancelled-network-changed"); return; }
  if (iotCurrent == iotSelf) { ++iotCurrent; iotPortIndex = 0; }
  if (iotCurrent > iotLast) { finishIot("complete"); return; }
  WiFiClient client;
  const IPAddress targetIp = numberIp(iotCurrent);
  const Service& service = services[iotPortIndex];
  const bool open = client.connect(targetIp, service.port, 150);
  client.stop();
  ++iotChecks;
  if (open) {
    ++iotOpen;
    Serial.printf("[iotrecon] open ip=%s port=%u service=%s\n", targetIp.toString().c_str(), service.port, service.label);
  }
  #ifdef HAS_SD
  if (!iotFile || !sd_obj.supported) { finishIot("error-SD-unavailable"); return; }
  if (fprintf(iotFile, "%s,%lu,%s,%u,%s,%s\n", clockUtc().c_str(), (unsigned long)millis(),
      targetIp.toString().c_str(), service.port, service.label, open ? "open" : "closed-or-filtered") < 0) {
    finishIot("error-SD-write"); return;
  }
  if (iotPortIndex + 1 == serviceCount &&
      (fflush(iotFile) != 0 || fsync(fileno(iotFile)) != 0)) {
    finishIot("error-SD-flush"); return;
  }
  #endif
  if (++iotPortIndex >= serviceCount) {
    iotPortIndex = 0;
    announce("[iotrecon] checked " + targetIp.toString());
    ++iotCurrent;
  }
}
}
