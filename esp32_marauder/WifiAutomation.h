#pragma once
#include <Arduino.h>
#include <IPAddress.h>
namespace marauder {
bool handleWifiAutomationCommand(const String& input);
void wifiAutomationTick(uint32_t now);
void stopWifiAutomation();
// True means a pending automation consumed the frame; skip the normal sniffer UI.
bool observeAutomationProbe(const uint8_t* frame, size_t length);
struct WifiAutomationStatus {
  bool probeWaiting;
  bool probePortal;
  bool iotRunning;
  String probeSsid;
  String notice;
  String iotResult;
  String reportPath;
  IPAddress currentIp;
  uint32_t checks;
  uint32_t openPorts;
};
WifiAutomationStatus wifiAutomationStatus();
}
