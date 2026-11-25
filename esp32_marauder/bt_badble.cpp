#include "configs.h"
#include "bt_badble.h"
#include "BadBleScript.h"
#ifdef HAS_SD
#include "SDInterface.h"
extern SDInterface sd_obj;
#endif
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "nimble/nimble/host/include/host/ble_gatt.h"
#include <atomic>
namespace {
NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* input = nullptr;
std::atomic<bool> connected{false}, subscribed{false}, encrypted{false};
std::atomic<uint16_t> peerHandle{0xffff};
std::atomic<uint32_t> connectionGeneration{0};
bool serving = false;
uint32_t runs = 0, runConnection = 0;
specter::BadBleScript script;
specter::BadBleRunner runner;
String selectedName, notice = "Choose a script, then Run";
bool runLogOpen = false;
std::atomic<uint32_t> queuedReports{0}, sentReports{0};
std::atomic<int> reportError{0};
constexpr char scriptsDirectory[] = "/btloot/payloads";
constexpr char sdDemo[] = "REM Specter SD keyboard test\nDEFAULTDELAY 30\nDELAY 1000\nSTRING Hello from Specter SD\nDELAY 250\nSTRING .\nREPEAT 2\n";
void logEvent(const char* event) {
  if (!runLogOpen) return;
  bt_loot_writeln(String("{\"module\":\"badble\",\"type\":\"") + event +
    "\",\"script\":\"" + bt_json_escape(selectedName) + "\",\"uptime_ms\":" + String(millis()) +
    ",\"step\":" + String(runner.step) + ",\"characters\":" + String(runner.characters) + "}");
}
void finishLog(const char* event) {
  logEvent(event);
  if (runLogOpen) { bt_loot_close(); runLogOpen = false; }
}
// HOGP uses an eight-byte value; Report ID is conveyed by descriptor 0x2908.
uint8_t reportMap[] = {
  0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,
  0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,
  0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,
  0x81,0x01,0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,
  0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xc0
};
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    if (connected) { server->disconnect(info.getConnHandle()); return; }
    peerHandle = info.getConnHandle();
    connected = true; subscribed = false; encrypted = false;
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) override {
    if (info.getConnHandle() != peerHandle) return;
    connected = false; subscribed = false; encrypted = false;
    ++connectionGeneration;
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if(info.getConnHandle() == peerHandle) encrypted = info.isEncrypted();
  }
} serverCallbacks;
class InputCallbacks : public NimBLECharacteristicCallbacks {
  void onStatus(NimBLECharacteristic*, NimBLEConnInfo&, int code) override {
    // NimBLE 2.3.8 does not populate ConnInfo in its notify-TX callback.
    // This callback belongs only to our input report and one host is accepted.
    if (!code) ++sentReports;
    else { int expected = 0; reportError.compare_exchange_strong(expected, code); }
  }
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info, uint16_t value) override {
    if(info.getConnHandle() != peerHandle) return;
    encrypted = info.isEncrypted();
    subscribed = (value & 1) != 0;
    if (!subscribed) ++connectionGeneration;
  }
} inputCallbacks;
bool report(const uint8_t (&value)[8]) {
  if (!input || !connected || !subscribed || !encrypted) return false;
  input->setValue(value, sizeof(value));
  // Copy each press/release into its own notification to this peer. The
  // zero-argument notify() only schedules a characteristic-value update and
  // returns true even when no individual notification was queued.
  const bool queued = input->notify(value, sizeof(value), peerHandle.load());
  if (queued) ++queuedReports;
  else { int expected = 0; reportError.compare_exchange_strong(expected, -1); }
  return queued;
}
void release() { const uint8_t empty[8] = {}; report(empty); }
bool registeredReport() {
  if (!input || !bt_radio_ready()) return false;
  const NimBLEUUID service(uint16_t(0x1812)), characteristic(uint16_t(0x2a4d));
  uint16_t handle = 0;
  return ble_gatts_find_chr(service.getBase(), characteristic.getBase(), nullptr, &handle) == 0 &&
    handle != 0 && handle == input->getHandle();
}
bool start() {
  if (!bt_is_armed()) { Serial.println("[bb] Enable Red Team and arm BT+ first"); return false; }
  bt_stop_all();
  bt_radio_end();
  if (!bt_radio_begin()) return false;
  auto* server = NimBLEDevice::createServer();
  if (!server) return false;
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);
  NimBLEDevice::setDeviceName("Specter Keyboard");
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  if (!hid) {
    hid = new NimBLEHIDDevice(server);
    hid->setManufacturer("Specter");
    hid->setHidInfo(0, 2);
    hid->setReportMap(reportMap, sizeof(reportMap));
    input = hid->getInputReport(1);
    if (!input) return false;
    input->setCallbacks(&inputCallbacks);
    hid->setBatteryLevel(100);
  }
  hid->startServices();
  auto* adv = NimBLEDevice::getAdvertising();
  adv->reset();
  adv->setName("Specter Keyboard");
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(NimBLEUUID(uint16_t(0x1812)));
  adv->enableScanResponse(true);
  connected = false; subscribed = false; encrypted = false;
  queuedReports = 0; sentReports = 0; reportError = 0;
  serving = adv->start();
  if (serving && !registeredReport()) {
    adv->stop(); serving = false; notice = "Keyboard GATT registration failed";
    Serial.println("[bb] " + notice); return false;
  }
  if (selectedName.isEmpty()) bt_badble_select("demo-hello");
  Serial.println(serving ? "[bb] Pair Specter Keyboard; choose a script and tap Run" : "[bb] Advertising failed");
  return serving;
}
}

// Called only after successful deinit(true); NimBLE owns the actual services.
// The HID wrapper has non-owning pointers and must not survive their deletion.
void bt_badble_host_released() {
  delete hid; hid = nullptr; input = nullptr;
  serving = false; connected = false; subscribed = false; encrypted = false;
  peerHandle = 0xffff;
  ++connectionGeneration;
}
bool bt_badble_busy() { return serving; }
bool bt_badble_ready() { return serving && connected && subscribed && encrypted && bt_is_armed(); }
bool bt_badble_typing() { return runner.running; }
String bt_badble_selected() { return selectedName; }
String bt_badble_notice() { return notice; }
namespace {
bool scriptName(const String& name) {
  if (!name.length() || name.length() > 63 || name == "." || name == "..") return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const unsigned char c = name[i];
    if (!isalnum(c) && c != ' ' && c != '_' && c != '-' && c != '.') return false;
  }
  return true;
}
bool scriptExtension(String name) {
  name.toLowerCase();
  return name.endsWith(".txt") || name.endsWith(".duck") || name.endsWith(".ducky");
}
bool loadBody(const String& name, std::string& body) {
  if (name == "demo-hello") { body = "DEFAULTDELAY 100\nSTRING Hello from Specter\n"; return true; }
  if (!scriptName(name)) { notice = "Invalid script filename"; return false; }
#ifdef HAS_SD
  if (!sd_obj.supported) { notice = "SD card unavailable"; return false; }
  const String path = String(scriptsDirectory) + "/" + name;
  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory()) { notice = "Script not found on SD"; return false; }
  const size_t size = file.size();
  if (!size || size > specter::badBleMaxBytes) { file.close(); notice = "Script must be 1..32768 bytes"; return false; }
  body.resize(size);
  const size_t read = file.read(reinterpret_cast<uint8_t*>(&body[0]), size);
  file.close();
  if (read != size) { body.clear(); notice = "SD read failed"; return false; }
  return true;
#else
  notice = "SD card unavailable"; return false;
#endif
}
bool compileBody(const std::string& body, const String& name) {
  selectedName = ""; // A failed load must never leave an old runnable payload.
  if (!script.compile(body)) {
    notice = String("Line ") + String(script.errorLine) + ": " + script.error;
    Serial.println("[bb] " + notice); return false;
  }
  runner = specter::BadBleRunner{};
  selectedName = name;
  notice = String(script.count) + " steps, " + String(script.characters) + " characters";
  Serial.println("[bb] Selected " + name + ": " + notice);
  return true;
}
bool installDemo() {
#ifdef HAS_SD
  if (!sd_obj.supported) { notice = "SD card unavailable"; return false; }
  SD.mkdir("/btloot"); SD.mkdir(scriptsDirectory);
  const String path = String(scriptsDirectory) + "/hello-specter.txt";
  if (SD.exists(path)) { notice = "SD demo already exists; preserved"; return true; }
  File file = SD.open(path, FILE_WRITE);
  if (!file) { notice = "Cannot create SD demo"; return false; }
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(sdDemo), sizeof(sdDemo)-1) == sizeof(sdDemo)-1;
  file.flush(); file.close();
  if (!ok) { SD.remove(path); notice = "SD demo write failed"; return false; }
  notice = "Created hello-specter.txt"; return true;
#else
  notice = "SD card unavailable"; return false;
#endif
}
}
size_t bt_badble_scripts(String* names, size_t capacity, size_t offset, bool& more) {
  size_t seen = 0, count = 0; more = false;
  auto add = [&](const String& name) {
    if (seen++ < offset) return;
    if (count < capacity) names[count++] = name;
    else more = true;
  };
  add("demo-hello");
#ifdef HAS_SD
  if (sd_obj.supported) {
    File dir = SD.open(scriptsDirectory);
    if (dir && dir.isDirectory()) {
      while (File file = dir.openNextFile()) {
        String name = file.name(); name = name.substring(name.lastIndexOf('/')+1);
        if (!file.isDirectory() && scriptName(name) && scriptExtension(name)) add(name);
        file.close();
        if (more) break;
      }
      dir.close();
    }
  }
#endif
  return count;
}
bool bt_badble_select(const String& name) {
  if (runner.running) { notice = "Stop current script first"; return false; }
  selectedName = ""; script.count = 0;
  std::string body;
  if (!loadBody(name, body)) { Serial.println("[bb] " + notice); return false; }
  return compileBody(body, name);
}
bool bt_badble_run_selected() {
  auto refuse = [](const char* reason) { notice = reason; Serial.println("[bb] " + notice); return false; };
  if (runner.running) return refuse("Script already running");
  if (!bt_badble_ready()) return refuse("Pair host and wait for Ready");
  if (!selectedName.length() || !script.count) return refuse("Choose a valid script first");
  reportError = 0;
  runner.start(millis()); runConnection = connectionGeneration;
  notice = "Starting in 1.5s";
  runLogOpen = bt_loot_open("badble");
  if (!runLogOpen) Serial.println("[bb] Run has no event log: storage unavailable");
  logEvent("run_start");
  Serial.println("[bb] Running " + selectedName);
  return true;
}
void bt_badble_stop() {
  runner.stop(report, "Stopped"); finishLog("run_abort"); notice = "Stopped";
  if (!serving) return;
  release(); serving = false;
  auto* server = NimBLEDevice::getServer();
  if (server) {
    server->advertiseOnDisconnect(false);
    for (auto handle : server->getPeerDevices()) server->disconnect(handle);
  }
  NimBLEDevice::getAdvertising()->stop();
  connected = false; subscribed = false; encrypted = false;
  ++connectionGeneration;
  Serial.println("[bb] Keyboard stopped");
}
bool bt_badble_command(const String& command) {
  if (command == "bb serve") { start(); return true; }
  if (command == "bb stop") { bt_badble_stop(); return true; }
  if (command == "bb list") {
    String names[6]; bool more = false; size_t offset = 0;
    do {
      const size_t n = bt_badble_scripts(names, 6, offset, more);
      for(size_t i=0;i<n;++i) Serial.println("[bb] script=" + names[i]);
      offset += n;
    } while(more);
    Serial.println("[bb] Scripts: /btloot/payloads/*.txt, *.duck, *.ducky"); return true;
  }
  if (command == "bb status") {
    Serial.println(String("[bb] serving=") + (serving?"yes":"no") + " ready=" + (bt_badble_ready()?"yes":"no") + " registered=" + (registeredReport()?"yes":"no") +
      " connected=" + (connected?"yes":"no") + " encrypted=" + (encrypted?"yes":"no") +
      " notifications=" + (subscribed?"yes":"no") + " protocol=" + String(hid && hid->getProtocolMode()->getValue().size() ? hid->getProtocolMode()->getValue().data()[0] : 255) +
      " queued=" + String(queuedReports.load()) + " tx=" + String(sentReports.load()) + " report_error=" + String(reportError.load()) + " running=" + (runner.running?"yes":"no") + " selected=" + selectedName + " state=" + notice); return true;
  }
  if (command == "bb install-demo") {
    if (runner.running) notice = "Stop current script first";
    else installDemo();
    Serial.println("[bb] " + notice); return true;
  }
  if (command == "bb hello") { if (!runner.running && bt_badble_select("demo-hello")) bt_badble_run_selected(); return true; }
  if (command == "bb run") { bt_badble_run_selected(); return true; }
  if (command.startsWith("bb run ") || command.startsWith("bb validate ")) {
    const bool validate = command.startsWith("bb validate ");
    String name = command.substring(validate ? 12 : 7); name.trim();
    if (name.startsWith("\"") && name.endsWith("\"") && name.length() >= 2) name = name.substring(1, name.length()-1);
    if (name != "demo-hello" && !scriptExtension(name)) name += ".txt";
    if (bt_badble_select(name) && !validate) bt_badble_run_selected();
    return true;
  }
  if (command.startsWith("bb type ") || command.startsWith("bb key ")) {
    if (runner.running) { Serial.println("[bb] Stop current script first"); return true; }
    const bool text = command.startsWith("bb type ");
    const String body = (text ? String("STRING ") : String("")) + command.substring(text ? 8 : 7);
    if (compileBody(std::string(body.c_str(), body.length()), text ? "CLI text" : "CLI key")) bt_badble_run_selected();
    return true;
  }
  if (command == "bb script") {
    Serial.println("[bb] REM, DELAY 0..60000, DEFAULTDELAY/DEFAULT_DELAY 0..5000, STRING, STRINGLN, REPEAT 1..1024");
    Serial.println("[bb] CTRL SHIFT ALT GUI + one named key/character; US ASCII layout; max 1024 steps / 32 KiB"); return true;
  }
  if (command == "bb" || command.startsWith("bb ")) {
    Serial.println("bb serve | bb hello | bb list | bb validate <file> | bb run [file] | bb stop");
    Serial.println("bb install-demo | bb status | bb script | bb type <text> | bb key <combo>");
    return true;
  }
  return false;
}
void bt_badble_loop() {
  if (serving && !bt_is_armed()) { bt_badble_stop(); return; }
  const bool wasRunning = runner.running;
  runner.tick(script, millis(), bt_badble_ready() && runConnection == connectionGeneration && reportError.load() == 0, report);
  if (wasRunning) notice = runner.result;
  if (wasRunning && !runner.running) {
    if (runner.completed) ++runs;
    finishLog(runner.completed ? "run_done" : "run_abort");
    Serial.println("[bb] " + notice);
  }
}
bool bt_badble_status(BtUiState& out) {
  if (!serving) return false;
  out.title = "BadBLE Keyboard"; out.module = BT_MOD_BADBLE; out.running = true;
  out.phase = runner.running ? runner.result : !connected ? "Pair your computer" :
    !encrypted ? "Waiting for encryption" : !subscribed ? "Waiting for host reports" : "Ready";
  out.lineCount = 0;
  out.lines[out.lineCount++] = connected ? String("Queued ") + String(queuedReports.load()) + " TX " + String(sentReports.load()) + " err " + String(reportError.load()) : String("Name: Specter Keyboard");
  out.lines[out.lineCount++] = "Script: " + selectedName;
  out.lines[out.lineCount++] = String("Step ") + String(runner.step) + "/" + String(script.count) + " chars " + String(runner.characters);
  out.lines[out.lineCount++] = notice;
  return true;
}
